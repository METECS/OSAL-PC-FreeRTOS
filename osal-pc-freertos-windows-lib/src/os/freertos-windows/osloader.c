/*
 *  NASA Docket No. GSC-18,370-1, and identified as "Operating System Abstraction Layer"
 *
 *  Copyright (c) 2019 United States Government as represented by
 *  the Administrator of the National Aeronautics and Space Administration.
 *  All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/**
 * \file   osloader.c
 * \author Christopher Sullivan based on work by joseph.p.hickey@nasa.gov and Jonathan Brandenburg
 *
 * Purpose: This file contains the module loader and symbol lookup functions for the OSAL.
 */

#include "os-FreeRTOS.h"
#include "ff_stdio.h"

/*
 ** If OS_INCLUDE_MODULE_LOADER is not defined, then skip the module
 */
#ifdef OS_INCLUDE_MODULE_LOADER

#include "simplestaticloader.h"

/****************************************************************************************
 TYPEDEFS
 ****************************************************************************************/

typedef struct
{
	char SymbolName[OS_MAX_SYM_LEN];
	cpuaddr SymbolAddress;
} SymbolRecord_t;

/****************************************************************************************
 DEFINES
 ****************************************************************************************/

#define OS_SYMBOL_RECORD_SIZE sizeof(SymbolRecord_t)

/****************************************************************************************
 GLOBAL DATA
 ****************************************************************************************/

/*
 * The "OS_module_internal_record_t" structure is used internally
 * to the osloader module for keeping the state.  It is OS-specific
 * and should not be directly used by external entities.
 */
typedef struct
{
	int free;
	uint32 host_module_id;
} OS_impl_module_internal_record_t;

/*
 * The storage table is only instantiated when OS_MAX_MODULES is nonzero.
 * It is allowed to be zero to save memory in statically linked apps.
 * However even in that case it is still relevant to include the
 * OS_SymbolLookup_Impl() function for symbol lookups.
 *
 * If neither loading nor symbol lookups are desired then this file
 * shouldn't be used at all -- a no-op version should be used instead.
 */

OS_impl_module_internal_record_t OS_impl_module_table[OS_MAX_MODULES];

/*
 ** In addition to the module table, this is the static loader specific data.
 ** It is a mini symbol table with all of the information for the static loaded modules.
 */
static_load_file_header_t OS_symbol_table[OS_MAX_MODULES];

/****************************************************************************************
 INITIALIZATION FUNCTION
 ***************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_FreeRTOS_ModuleAPI_Impl_Init
 *
 *  Purpose: Local helper routine, not part of OSAL API.
 *
 *-----------------------------------------------------------------*/
int32 OS_FreeRTOS_ModuleAPI_Impl_Init(void)
{
	/*
	 ** Initialize Module Table
	 */
	for(int i = 0; i < OS_MAX_MODULES; i++)
	{
		OS_impl_module_table[i].free = TRUE;
		OS_impl_module_table[i].host_module_id = 0;
	}

	/*
	 ** Initialize Static file headers table
	 */
	memset(&(OS_symbol_table[0]), 0, sizeof(static_load_file_header_t) * OS_MAX_MODULES);

	return OS_SUCCESS;
} /* end OS_FreeRTOS_ModuleAPI_Impl_Init */

/****************************************************************************************
 Symbol table API
 ***************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_SymbolLookup_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SymbolLookup_Impl(cpuaddr *SymbolAddress, const char *SymbolName)
{
	/*
	 ** Lookup the symbol
	 */
	for(int i = 0; i < OS_MAX_MODULES; i++)
	{
		if((OS_impl_module_table[i].free == FALSE) && (strcmp((char*) SymbolName, OS_symbol_table[i].entry_point_name) == 0))
		{
			*SymbolAddress = (cpuaddr) OS_symbol_table[i].entry_point;
			return OS_SUCCESS;
		}
	}

	return OS_ERROR;
} /* end OS_SymbolLookup_Impl */


/*----------------------------------------------------------------
 *
 * Function: OS_SymbolTableDump_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SymbolTableDump_Impl(const char *local_filename, uint32 SizeLimit)
{
	FF_FILE *sym_table_file_fd;
	int32 return_status;
	int status;
	SymbolRecord_t symRecord;
	int i;

	if(SizeLimit < OS_SYMBOL_RECORD_SIZE)
	{
		return OS_ERROR;
	}

	/*
	 ** Open file. "open" returns -1 on error.
	 */
	sym_table_file_fd = ff_fopen(local_filename, "w");
	if(sym_table_file_fd != NULL)
	{
		/*
		 ** Lookup the symbol
		 */
		for(i = 0; i < OS_MAX_MODULES; i++)
		{
			if(OS_impl_module_table[i].free == FALSE)
			{
				/*
				 ** Copy symbol name
				 */
				strncpy(symRecord.SymbolName, OS_symbol_table[i].entry_point_name, OS_MAX_SYM_LEN);

				/*
				 ** Save symbol address
				 */
				symRecord.SymbolAddress = (cpuaddr) OS_symbol_table[i].entry_point;

				/*
				 ** Write entry in file
				 */
				status = ff_fwrite(&symRecord, 1, sizeof(symRecord), sym_table_file_fd);
				if(status == -1)
				{
					ff_fclose(sym_table_file_fd);
					return OS_ERROR;
				}
			} /* end if */
		} /* end for */
		return_status = OS_SUCCESS;
	}
	else
	{
		return_status = OS_ERROR;
	}

	ff_fclose(sym_table_file_fd);

	return return_status;
} /* end OS_SymbolTableDump_Impl */

/****************************************************************************************
 Module Loader API
 ****************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_ModuleLoad_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_ModuleLoad_Impl(uint32 local_id, char *translated_path)
{
	boolean StaticLoadStatus;

	//Claim module
	OS_impl_module_table[local_id].free = FALSE;

	/*
	 ** Load the module
	 */
	StaticLoadStatus = SimpleStaticLoadFile(translated_path, &OS_symbol_table[local_id]);
	if(StaticLoadStatus == FALSE)
	{
		OS_impl_module_table[local_id].free = TRUE;
		return OS_ERROR;
	}

	/*
	 ** fill out the OS_impl_module_table entry for this new module
	 */
	OS_impl_module_table[local_id].host_module_id = local_id;

	return OS_SUCCESS;
} /* end OS_ModuleLoad_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_ModuleUnload_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_ModuleUnload_Impl(uint32 local_id)
{
	OS_impl_module_table[local_id].free = TRUE;

	return OS_SUCCESS;
} /* end OS_ModuleUnload_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_ModuleGetInfo_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_ModuleGetInfo_Impl(uint32 local_id, OS_module_prop_t *module_prop)
{
	module_prop->host_module_id = (uint32) OS_impl_module_table[local_id].host_module_id;
	module_prop->addr.valid = TRUE;
	module_prop->addr.code_address = OS_symbol_table[local_id].code_target;
	module_prop->addr.code_size = OS_symbol_table[local_id].code_size;
	module_prop->addr.data_address = OS_symbol_table[local_id].data_target;
	module_prop->addr.data_size = OS_symbol_table[local_id].data_size;
	module_prop->addr.bss_address = OS_symbol_table[local_id].bss_target;
	module_prop->addr.bss_size = OS_symbol_table[local_id].bss_size;

	return OS_SUCCESS;
} /* end OS_ModuleGetInfo_Impl */

#endif /* OS_INCLUDE_MODULE_LOADER */
