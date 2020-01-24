/*
 * This file contains the module loader and symbol lookup functions for the OSAL.
 *   The implementation depends on a "simple static loader" API that was the result
 *   of a NASA internally developed static loader that is not yet available as
 *   open source. (Possibly referenced at https://software.nasa.gov/software/GSC-17810-1)
 *
 * Based on src/os/rtems/osloader.c from the OSAL distribution wit the
 * following license:
 *      Copyright (c) 2004-2006, United States government as represented by the
 *      administrator of the National Aeronautics Space Administration.
 *      All rights reserved. This software was created at NASAs Goddard
 *      Space Flight Center pursuant to government contracts.
 *
 *      This is governed by the NASA Open Source Agreement and may be used,
 *      distributed and modified only pursuant to the terms of that agreement.
 */
#include <fcntl.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "ff_stdio.h"

#include "common_types.h"
#include "osapi.h"

/*
 ** If OS_INCLUDE_MODULE_LOADER is not defined, then skip the module
 */
#ifdef OS_INCLUDE_MODULE_LOADER

#include "simplestaticloader.h"

/****************************************************************************************
 TYPEDEFS
 ****************************************************************************************/

typedef struct {
	char SymbolName[OS_MAX_SYM_LEN];
	cpuaddr SymbolAddress;
} SymbolRecord_t;

/****************************************************************************************
 DEFINES
 ****************************************************************************************/
#define OS_SYMBOL_RECORD_SIZE sizeof(SymbolRecord_t)

#undef OS_DEBUG_PRINTF

/****************************************************************************************
 GLOBAL DATA
 ****************************************************************************************/

/*
 * The "OS_module_internal_record_t" structure is used internally
 * to the osloader module for keeping the state.  It is OS-specific
 * and should not be directly used by external entities.
 */
typedef struct {
	int free;
	cpuaddr entry_point;
	uint32 host_module_id;
	char filename[OS_MAX_PATH_LEN];
	char name[OS_MAX_API_NAME];
	char original_name[MAX_API_NAME_INCOMING];
} OS_module_internal_record_t;

/*
 ** Need to define the OS Module table here.
 ** osconfig.h will have the maximum number of loadable modules defined.
 */
OS_module_internal_record_t OS_module_table[OS_MAX_MODULES];

/*
 ** The Mutex for protecting the above table
 */
SemaphoreHandle_t OS_module_table_sem;

/*
 ** In addition to the module table, this is the static loader specific data.
 ** It is a mini symbol table with all of the information for the static loaded modules.
 */
static_load_file_header_t OS_symbol_table[OS_MAX_MODULES];

/* A counter to ensure generated names are unique */
static uint8 name_counter = 0;

/****************************************************************************************
 INITIALIZATION FUNCTION
 ****************************************************************************************/

int32 OS_ModuleTableInit(void) {
	int i;
    int32  status = OS_SUCCESS;

	/*
	 ** Initialize Module Table
	 */
	for (i = 0; i < OS_MAX_MODULES; i++) {
		OS_module_table[i].free = TRUE;
		OS_module_table[i].entry_point = 0;
		OS_module_table[i].host_module_id = 0;
		strcpy(OS_module_table[i].name, "");
		strcpy(OS_module_table[i].filename, "");
	}

	/*
	 ** Initialize Static file headers table
	 */
	memset(&(OS_symbol_table[0]), 0,
			sizeof(static_load_file_header_t) * OS_MAX_MODULES);

	/*
	 ** Create the Module Table mutex
	 */
	OS_module_table_sem = xSemaphoreCreateMutex();
	if (OS_module_table_sem == NULL) {
		status = OS_ERROR;
	}

	return (status);
}

/****************************************************************************************
 Symbol table API
 ****************************************************************************************/
/*--------------------------------------------------------------------------------------
 Name: OS_SymbolLookup

 Purpose: Find the Address of a Symbol

 Parameters:

 Returns: OS_ERROR if the symbol could not be found
 OS_SUCCESS if the symbol is found
 OS_INVALID_POINTER if one of the pointers passed in are NULL
 ---------------------------------------------------------------------------------------*/
int32 OS_SymbolLookup(cpuaddr *SymbolAddress, const char *SymbolName) {
	int i;

	/*
	 ** Check parameters
	 */
	if ((SymbolAddress == NULL) || (SymbolName == NULL)) {
		return (OS_INVALID_POINTER);
	}

	/*
	 ** Static load lookup. Since we are not maintaining a symbol table
	 ** in a static linked implementation, only the entry point symbols are saved.
	 */
	xSemaphoreTake(OS_module_table_sem, portMAX_DELAY);
	/*
	 ** Lookup the symbol
	 */
	for (i = 0; i < OS_MAX_MODULES; i++) {
		if ((OS_module_table[i].free == FALSE)
				&& (strcmp((char*) SymbolName,
						OS_symbol_table[i].entry_point_name) == 0)) {
			*SymbolAddress = (cpuaddr) OS_symbol_table[i].entry_point;
			xSemaphoreGive(OS_module_table_sem);
			return OS_SUCCESS;
		}
	}
	xSemaphoreGive(OS_module_table_sem);
	return (OS_ERROR);
}/* end OS_SymbolLookup */

/*--------------------------------------------------------------------------------------
 Name: OS_SymbolTableDump

 Purpose: Dumps the system symbol table to a file

 Parameters:

 Returns: OS_ERROR if the symbol table could not be read or dumped
 OS_FS_ERR_PATH_INVALID  if the file and/or path is invalid
 OS_SUCCESS if the file is written correctly
 ---------------------------------------------------------------------------------------*/
int32 OS_SymbolTableDump(const char *filename, uint32 SizeLimit) {
	char local_path_name[OS_MAX_LOCAL_PATH_LEN];
	FF_FILE *sym_table_file_fd;
	int32 return_status;
	int status;
	SymbolRecord_t symRecord;
	int i;

	/*
	 ** Check parameters
	 */
	if (filename == NULL) {
		return OS_INVALID_POINTER;
	}
	if (SizeLimit < OS_SYMBOL_RECORD_SIZE) {
		return (OS_ERROR);
	}

	/*
	 ** Get local path name
	 */
	if (OS_TranslatePath(filename, local_path_name) != OS_FS_SUCCESS) {
		return (OS_FS_ERR_PATH_INVALID);
	}

	/*
	 ** Open file. "open" returns -1 on error.
	 */
	sym_table_file_fd = ff_fopen(local_path_name, "w");
	if (sym_table_file_fd != NULL) {
		/*
		 ** Iterate the symbol table
		 */
		xSemaphoreTake(OS_module_table_sem, portMAX_DELAY);

		/*
		 ** Lookup the symbol
		 */
		for (i = 0; i < OS_MAX_MODULES; i++) {
			if (OS_module_table[i].free == FALSE) {
				/*
				 ** Copy symbol name
				 */
				strncpy(symRecord.SymbolName,
						OS_symbol_table[i].entry_point_name, OS_MAX_SYM_LEN);

				/*
				 ** Save symbol address
				 */
				symRecord.SymbolAddress =
						(cpuaddr) OS_symbol_table[i].entry_point;

				/*
				 ** Write entry in file
				 */
				status = ff_fwrite(&symRecord, 1, sizeof(symRecord),
						sym_table_file_fd);
				if (status == -1) {
					xSemaphoreGive(OS_module_table_sem);
					ff_fclose(sym_table_file_fd);
					return (OS_ERROR);
				}
			} /* end if */
		} /* end for */
		xSemaphoreGive(OS_module_table_sem);
		return_status = OS_SUCCESS;
	} else {
		return_status = OS_ERROR;
	}

	ff_fclose(sym_table_file_fd);

	return (return_status);
}/* end OS_SymbolTableDump */

/****************************************************************************************
 Module Loader API
 ****************************************************************************************/

/*--------------------------------------------------------------------------------------
 Name: OS_ModuleLoad

 Purpose: Loads an ELF object file into the running operating system

 Parameters:

 Returns: OS_ERROR if the module cannot be loaded
 OS_INVALID_POINTER if one of the parameters is NULL
 OS_ERR_NO_FREE_IDS if the module table is full
 OS_ERR_NAME_TAKEN if the name is in use
 OS_SUCCESS if the module is loaded successfuly
 ---------------------------------------------------------------------------------------*/
int32 OS_ModuleLoad(uint32 *module_id, const char *module_name,
		const char *filename) {
	int i;
	uint32 possible_moduleid;
	char translated_path[OS_MAX_LOCAL_PATH_LEN];
	int32 return_code;
	boolean StaticLoadStatus;
	/*
	 ** Check parameters
	 */
	if ((filename == NULL) || (module_id == NULL) || (module_name == NULL)) {
#ifdef OS_DEBUG_PRINTF
		OS_printf("OSAL: Error, invalid parameters to OS_ModuleLoad\n");
#endif
		return (OS_INVALID_POINTER);
	}

	xSemaphoreTake(OS_module_table_sem, portMAX_DELAY);

	/*
	 ** Find a free module id
	 */
	for (possible_moduleid = 0; possible_moduleid < OS_MAX_MODULES;
			possible_moduleid++) {
		if (OS_module_table[possible_moduleid].free == TRUE) {
			break;
		}
	}

	/*
	 ** Check to see if the id is out of bounds
	 */
	if (possible_moduleid >= OS_MAX_MODULES) {
		xSemaphoreGive(OS_module_table_sem);
		return OS_ERR_NO_FREE_IDS;
	}

	/*
	 ** Check to see if the module file is already loaded
	 */
	for (i = 0; i < OS_MAX_MODULES; i++) {
		if ((OS_module_table[i].free == FALSE)
				&& (strcmp((char*) module_name, OS_module_table[i].name) == 0)) {
			xSemaphoreGive(OS_module_table_sem);
			return OS_ERR_NAME_TAKEN;
		}
	}

	/*
	 ** Set the possible Module ID to "not free" so that
	 ** another caller will not try to use it.
	 */
	OS_module_table[possible_moduleid].free = FALSE;
	xSemaphoreGive(OS_module_table_sem);

	/*
	 ** Translate the filename to the Host System
	 */
	return_code = OS_TranslatePath((const char *) filename,
			(char *) translated_path);
	if (return_code != OS_SUCCESS) {
		OS_module_table[possible_moduleid].free = TRUE;
		return (return_code);
	}

	/*
	 ** Load the module
	 */
	StaticLoadStatus = SimpleStaticLoadFile(translated_path,
			&OS_symbol_table[possible_moduleid]);
	if (StaticLoadStatus == FALSE) {
		OS_module_table[possible_moduleid].free = TRUE;
#ifdef OS_DEBUG_PRINTF
		OS_printf("OSAL: Error, cannot load static module: %s\n",translated_path);
#endif
		return (OS_ERROR);
	}
#ifdef OS_DEBUG_PRINTF
	OS_printf("OSAL: Loaded Module OK.\n");
#endif

	/*
	 ** fill out the OS_module_table entry for this new module
	 */
	OS_module_table[possible_moduleid].entry_point =
			OS_symbol_table[possible_moduleid].entry_point;
	OS_module_table[possible_moduleid].host_module_id = possible_moduleid;

	strncpy(OS_module_table[possible_moduleid].filename, filename,
			OS_MAX_PATH_LEN);
	strncpy(OS_module_table[possible_moduleid].original_name, module_name,
			MAX_API_NAME_INCOMING);
	char os_unique_name[OS_MAX_API_NAME];
	snprintf(os_unique_name, OS_MAX_API_NAME, "%-*.*s%02x", (OS_MAX_API_NAME-3), (OS_MAX_API_NAME-3), module_name, name_counter++);
	strncpy(OS_module_table[possible_moduleid].name, os_unique_name,
			OS_MAX_API_NAME);

	/*
	 ** Return the OSAL Module ID
	 */
	*module_id = possible_moduleid;

	return (OS_SUCCESS);

}/* end OS_ModuleLoad */

/*--------------------------------------------------------------------------------------
 Name: OS_ModuleUnload

 Purpose: Unloads the module file from the running operating system

 Parameters:

 Returns: OS_ERROR if the module is invalid or cannot be unloaded
 OS_SUCCESS if the module was unloaded successfuly
 ---------------------------------------------------------------------------------------*/
int32 OS_ModuleUnload(uint32 module_id) {
	/*
	 ** Check the module_id
	 */
	if (module_id >= OS_MAX_MODULES || OS_module_table[module_id].free == TRUE) {
		return (OS_ERR_INVALID_ID);
	}

	/*
	 ** Free the module entry
	 */
	OS_module_table[module_id].free = TRUE;

	return (OS_SUCCESS);
}/* end OS_ModuleUnload */

/*--------------------------------------------------------------------------------------
 Name: OS_ModuleInfo

 Purpose: Returns information about the loadable module

 Parameters:

 Returns: OS_INVALID_POINTER if the pointer to the ModuleInfo structure is NULL
 OS_ERR_INVALID_ID if the module ID is not valid
 OS_SUCCESS if the module info was filled out successfuly
 ---------------------------------------------------------------------------------------*/
int32 OS_ModuleInfo(uint32 module_id, OS_module_prop_t *module_info) {
	/*
	 ** Check the parameter
	 */
	if (module_info == NULL) {
		return (OS_INVALID_POINTER);
	}

	/*
	 ** Check the module_id
	 */
	if (module_id >= OS_MAX_MODULES || OS_module_table[module_id].free == TRUE) {
		return (OS_ERR_INVALID_ID);
	}

	/*
	 ** Fill out the module info
	 */
	module_info->entry_point = OS_module_table[module_id].entry_point;
	module_info->host_module_id =
			(uint32) OS_module_table[module_id].host_module_id;
	strncpy(module_info->filename, OS_module_table[module_id].filename,
			OS_MAX_PATH_LEN);
	strncpy(module_info->name, OS_module_table[module_id].original_name,
			MAX_API_NAME_INCOMING);

	module_info->addr.valid = TRUE;
	module_info->addr.code_address = OS_symbol_table[module_id].code_target;
	module_info->addr.code_size = OS_symbol_table[module_id].code_size;
	module_info->addr.data_address = OS_symbol_table[module_id].data_target;
	module_info->addr.data_size = OS_symbol_table[module_id].data_size;
	module_info->addr.bss_address = OS_symbol_table[module_id].bss_target;
	module_info->addr.bss_size = OS_symbol_table[module_id].bss_size;
	module_info->addr.flags = OS_symbol_table[module_id].flags;

	return (OS_SUCCESS);
}/* end OS_ModuleInfo */

#endif /* OS_INCLUDE_MODULE_LOADER */
