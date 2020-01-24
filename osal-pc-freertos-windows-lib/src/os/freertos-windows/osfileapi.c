/*
 * This file contains all of the api calls for manipulating
 * files in a file system for FreeRTOS
 *
 * Based on src/os/rtems/osfileapi.c from the OSAL distribution wit the
 * following license:
 *      Copyright (c) 2004-2006, United States government as represented by the
 *      administrator of the National Aeronautics Space Administration.
 *      All rights reserved. This software was created at NASAs Goddard
 *      Space Flight Center pursuant to government contracts.
 *
 *      This is governed by the NASA Open Source Agreement and may be used,
 *      distributed and modified only pursuant to the terms of that agreement.
 */

#include "FreeRTOS.h"
#include "semphr.h"
#include "ff_stdio.h"

#include "common_types.h"
#include "osapi.h"
#include "osapi-os-filesys.h"

#include "osapi-os-filesys-ex.h"
#include "dirent.h"
#include "sys/stat.h"

/***************************************************************************************
 DATA TYPES
 ***************************************************************************************/
enum OS_DirTableEntryState {
	DirTableEntryStateUndefined,
	DirTableEntryStateAfterFindFirst,
	DirTableEntryStateAfterFindNext
};

typedef struct {
	int32 VolumeType;
	char Path[OS_MAX_PATH_LEN]; /* The path of the file opened */
	union {
		FF_FindData_t xFindData; /* The current state of the directory search */
		DIR *dp;
	} dir;
	os_dirent_t entry; /* The information about the current entry */
	uint8 IsValid; /* Whether or not this entry is valid */
	enum OS_DirTableEntryState state;
} OS_DirTableEntry;

typedef struct {
	int32 OSfd;
	void *OSfp;
} OS_FDToFPMappingEntry;

/* Provide something to implement os_dirp_t */
typedef void * os_dirp_t;

/***************************************************************************************
 FUNCTION PROTOTYPES
 ***************************************************************************************/

int32 OS_check_name_length(const char *path);
extern uint32 OS_FindCreator(void);

/*
 * Opens a directory for searching
 * Replaced by OS_DirectoryOpen()
 */
os_dirp_t       OS_opendir (const char *path);

/*
 * Closes an open directory
 * Replaced by OS_DirectoryClose()
 */
int32           OS_closedir(os_dirp_t directory);

/*
 * Rewinds an open directory
 * Replaced by OS_DirectoryRewind()
 */
void            OS_rewinddir(os_dirp_t directory);

/*
 * Reads the next object in the directory
 * Replaced by OS_DirectoryRead()
 */
os_dirent_t *   OS_readdir (os_dirp_t directory);

/****************************************************************************************
 GLOBAL DATA
 ****************************************************************************************/

OS_file_prop_t OS_FDTable[OS_MAX_NUM_OPEN_FILES];
OS_file_prop_Ex_t OS_FDTableEx[OS_MAX_NUM_OPEN_FILES];
OS_FDToFPMappingEntry OS_FDToFPMapping[OS_MAX_NUM_OPEN_FILES];
OS_DirTableEntry OS_DirTable[OS_MAX_NUM_OPEN_FILES];
SemaphoreHandle_t OS_FDTableSem;
SemaphoreHandle_t OS_DirTableSem;
SemaphoreHandle_t OS_VolumeTableSem;

/****************************************************************************************
 INITIALIZATION FUNCTION
 ****************************************************************************************/
int32 OS_FS_Init(void) {
	int i;
	BaseType_t status;

	/* Initialize the file system constructs */
	for (i = 0; i < OS_MAX_NUM_OPEN_FILES; i++) {
		OS_FDTableEx[i].OSfd = -1;
		strcpy(OS_FDTable[i].Path, "\0");
		OS_FDTable[i].User = 0;
		OS_FDTable[i].IsValid = FALSE;
	}

	for (i = 0; i < OS_MAX_NUM_OPEN_FILES; i++) {
		OS_FDToFPMapping[i].OSfd = -1;
		OS_FDToFPMapping[i].OSfp = NULL;
	}

	/* Initialize the directory search constructs */
	for (i = 0; i < OS_MAX_NUM_OPEN_FILES; i++) {
		strcpy(OS_DirTable[i].Path, "\0");
		OS_DirTable[i].VolumeType = -1;
		OS_DirTable[i].IsValid = FALSE;
		OS_DirTable[i].state = DirTableEntryStateUndefined;
	}

	/*
	 ** Initialize the FS subsystem semaphore
	 */
	OS_FDTableSem = xSemaphoreCreateMutex();
	if (OS_FDTableSem == NULL) {
		status = OS_ERROR;
		return (status);
	}

	/*
	 ** Initialize the system call lock semaphore
	 */
	OS_VolumeTableSem = xSemaphoreCreateMutex();
	if (OS_VolumeTableSem == NULL) {
		status = OS_ERROR;
		return (status);
	}

	OS_DirTableSem = xSemaphoreCreateMutex();
	if (OS_DirTableSem == NULL) {
		status = OS_ERROR;
		return (status);
	}

	return (OS_SUCCESS);

}
/****************************************************************************************
 Filesys API
 ****************************************************************************************/

/*
 ** Standard File system API
 */

/*--------------------------------------------------------------------------------------
 Name: OS_creat

 Purpose: creates a file specified by const char *path, with read/write
 permissions by access. The file is also automatically opened by the
 create call.

 Returns: OS_FS_ERR_INVALID_POINTER if path is NULL
 OS_FS_ERR_PATH_TOO_LONG if path exceeds the maximum number of chars
 OS_FS_ERR_PATH_INVALID if path cannot be parsed
 OS_FS_ERR_NAME_TOO_LONG if the name of the file is too long
 OS_FS_ERROR if permissions are unknown or OS call fails
 OS_FS_ERR_NO_FREE_FDS if there are no free file descripors left
 File Descriptor is returned on success.

 ---------------------------------------------------------------------------------------*/

int32 OS_creat(const char *path, int32 access) {
	char local_path[OS_MAX_LOCAL_PATH_LEN];
	char *perm;
	uint32 PossibleFD;
	void *fp;
	int32 volume_type;

	/*
	 ** Check to see if the path pointer is NULL
	 */
	if (path == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	/*
	 ** Check to see if the path is too long
	 */
	if (strlen(path) >= OS_MAX_PATH_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	/*
	 ** check if the name of the file is too long
	 */
	if (OS_check_name_length(path) != OS_FS_SUCCESS) {
		return OS_FS_ERR_NAME_TOO_LONG;
	}

	/*
	 ** Check for a valid access mode
	 ** For creating a file, OS_READ_ONLY does not make sense
	 */
	switch (access) {
	case OS_WRITE_ONLY:
		perm = "wb";
		break;
	case OS_READ_WRITE:
		perm = "w+b";
		break;
	default:
		return OS_FS_ERROR;
	}

	/*
	 ** Translate the path
	 */
	if (OS_TranslatePath(path, (char *) local_path) != OS_FS_SUCCESS) {
		return OS_FS_ERR_PATH_INVALID;
	}

	/* Check Parameters */
	xSemaphoreTake(OS_FDTableSem, portMAX_DELAY);

	for (PossibleFD = 0; PossibleFD < OS_MAX_NUM_OPEN_FILES; PossibleFD++) {
		if (OS_FDTable[PossibleFD].IsValid == FALSE) {
			break;
		}
	}

	if (PossibleFD >= OS_MAX_NUM_OPEN_FILES) {
		xSemaphoreGive(OS_FDTableSem);
		return OS_FS_ERR_NO_FREE_FDS;
	}

	/*
	 ** Mark the table entry as valid so no other
	 ** task can take that ID
	 */
	OS_FDTable[PossibleFD].IsValid = TRUE;

	xSemaphoreGive(OS_FDTableSem);

	volume_type = OS_GetVolumeType(path);

	if (volume_type == RAM_DISK) {
		fp = ff_fopen(local_path, perm);

		if (fp != NULL) {
			xSemaphoreTake(OS_FDTableSem, portMAX_DELAY);
			/* fill in the table before returning */
			OS_FDTableEx[PossibleFD].OSfd = PossibleFD;
			OS_FDToFPMapping[PossibleFD].OSfd = PossibleFD;
			OS_FDToFPMapping[PossibleFD].OSfp = fp;
			strncpy(OS_FDTable[PossibleFD].Path, path, OS_MAX_PATH_LEN);
			OS_FDTable[PossibleFD].User = OS_FindCreator();
			xSemaphoreGive(OS_FDTableSem);
			return PossibleFD;
		} else {
			xSemaphoreTake(OS_FDTableSem, portMAX_DELAY);
			/* Operation failed, so reset to false */
			OS_FDTable[PossibleFD].IsValid = FALSE;
			xSemaphoreGive(OS_FDTableSem);
			return OS_FS_ERROR;
		}
	} else if (volume_type == FS_BASED) {
		fp = fopen(local_path, perm);

		if (fp != NULL) {
			xSemaphoreTake(OS_FDTableSem, portMAX_DELAY);
			/* fill in the table before returning */
			OS_FDTableEx[PossibleFD].OSfd = PossibleFD;
			OS_FDToFPMapping[PossibleFD].OSfd = PossibleFD;
			OS_FDToFPMapping[PossibleFD].OSfp = fp;
			strncpy(OS_FDTable[PossibleFD].Path, path, OS_MAX_PATH_LEN);
			OS_FDTable[PossibleFD].User = OS_FindCreator();
			xSemaphoreGive(OS_FDTableSem);
			return PossibleFD;
		} else {
			xSemaphoreTake(OS_FDTableSem, portMAX_DELAY);
			/* Operation failed, so reset to false */
			OS_FDTable[PossibleFD].IsValid = FALSE;
			xSemaphoreGive(OS_FDTableSem);
			return OS_FS_ERROR;
		}
	} else {
		return OS_FS_ERR_PATH_INVALID;
	}

} /* end OS_creat */

/*--------------------------------------------------------------------------------------
 Name: OS_open

 Purpose: Opens a file. access parameters are OS_READ_ONLY,OS_WRITE_ONLY, or
 OS_READ_WRITE

 Returns: OS_FS_ERR_INVALID_POINTER if path is NULL
 OS_FS_ERR_PATH_TOO_LONG if path exceeds the maximum number of chars
 OS_FS_ERR_PATH_INVALID if path cannot be parsed
 OS_FS_ERR_NAME_TOO_LONG if the name of the file is too long
 OS_FS_ERROR if permissions are unknown or OS call fails
 OS_FS_ERR_NO_FREE_FDS if there are no free file descriptors left
 a file descriptor if success
 ---------------------------------------------------------------------------------------*/

int32 OS_open(const char *path, int32 access, uint32 mode) {
	char local_path[OS_MAX_LOCAL_PATH_LEN];
	char *perm;
	uint32 PossibleFD;
	void *fp;
	int32 volume_type;

	/*
	 ** Check to see if the path pointer is NULL
	 */
	if (path == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	/*
	 ** Check to see if the path is too long
	 */
	if (strlen(path) >= OS_MAX_PATH_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	/*
	 ** check if the name of the file is too long
	 */
	if (OS_check_name_length(path) != OS_FS_SUCCESS) {
		return OS_FS_ERR_NAME_TOO_LONG;
	}

	/*
	 ** Check for a valid access mode
	 */
	switch (access) {
	case OS_READ_ONLY:
		perm = "rb";
		break;
	case OS_WRITE_ONLY:
		perm = "wb";
		break;
	case OS_READ_WRITE:
		perm = "w+b";
		break;
	default:
		return OS_FS_ERROR;
	}

	/*
	 ** Translate the path
	 */
	if (OS_TranslatePath(path, (char *) local_path) != OS_FS_SUCCESS) {
		return OS_FS_ERR_PATH_INVALID;
	}

	xSemaphoreTake(OS_FDTableSem, portMAX_DELAY);

	for (PossibleFD = 0; PossibleFD < OS_MAX_NUM_OPEN_FILES; PossibleFD++) {
		if (OS_FDTable[PossibleFD].IsValid == FALSE) {
			break;
		}
	}

	if (PossibleFD >= OS_MAX_NUM_OPEN_FILES) {
		xSemaphoreGive(OS_FDTableSem);
		return OS_FS_ERR_NO_FREE_FDS;
	}

	/*
	 ** Mark the table entry as valid so no other
	 ** task can take that ID
	 */
	OS_FDTable[PossibleFD].IsValid = TRUE;

	xSemaphoreGive(OS_FDTableSem);

	volume_type = OS_GetVolumeType(path);

	if (volume_type == RAM_DISK) {

		/* open the file  */
		fp = ff_fopen(local_path, perm);

		if (fp != NULL) {
			xSemaphoreTake(OS_FDTableSem, portMAX_DELAY);
			/* fill in the table before returning */
			OS_FDTableEx[PossibleFD].OSfd = PossibleFD;
			OS_FDToFPMapping[PossibleFD].OSfd = PossibleFD;
			OS_FDToFPMapping[PossibleFD].OSfp = fp;
			strncpy(OS_FDTable[PossibleFD].Path, path, OS_MAX_PATH_LEN);
			OS_FDTable[PossibleFD].User = OS_FindCreator();
			xSemaphoreGive(OS_FDTableSem);

			return PossibleFD;
		} else {
			xSemaphoreTake(OS_FDTableSem, portMAX_DELAY);
			/* Operation failed, so reset to false */
			OS_FDTable[PossibleFD].IsValid = FALSE;
			xSemaphoreGive(OS_FDTableSem);
			return OS_FS_ERROR;
		}
	} else if (volume_type == FS_BASED) {
		/* open the file  */
		fp = fopen(local_path, perm);

		if (fp != NULL) {
			xSemaphoreTake(OS_FDTableSem, portMAX_DELAY);
			/* fill in the table before returning */
			OS_FDTableEx[PossibleFD].OSfd = PossibleFD;
			OS_FDToFPMapping[PossibleFD].OSfd = PossibleFD;
			OS_FDToFPMapping[PossibleFD].OSfp = fp;
			strncpy(OS_FDTable[PossibleFD].Path, path, OS_MAX_PATH_LEN);
			OS_FDTable[PossibleFD].User = OS_FindCreator();
			xSemaphoreGive(OS_FDTableSem);

			return PossibleFD;
		} else {
			xSemaphoreTake(OS_FDTableSem, portMAX_DELAY);
			/* Operation failed, so reset to false */
			OS_FDTable[PossibleFD].IsValid = FALSE;
			xSemaphoreGive(OS_FDTableSem);
			return OS_FS_ERROR;
		}
	} else {
		return OS_FS_ERR_PATH_INVALID;

	}
} /* end OS_open */

/*--------------------------------------------------------------------------------------
 Name: OS_close

 Purpose: Closes a file.

 Returns: OS_FS_ERROR if file  descriptor could not be closed
 OS_FS_ERR_INVALID_FD if the file descriptor passed in is invalid
 OS_FS_SUCCESS if success
 ---------------------------------------------------------------------------------------*/

int32 OS_close(uint32 filedes) {
	int status;
	int32 volume_type;

	volume_type = OS_GetVolumeType(OS_FDTable[filedes].Path);

	/* Make sure the file descriptor is legit before using it */
	if (filedes
			< 0|| filedes >= OS_MAX_NUM_OPEN_FILES || OS_FDTable[filedes].IsValid == FALSE) {
		return OS_FS_ERR_INVALID_FD;
	} else if (volume_type == RAM_DISK) {
		status = ff_fclose(OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfp);
		if (status != 0) {
			/*
			 ** Remove the file from the OSAL list
			 ** to free up that slot
			 */
			/* fill in the table before returning */
			xSemaphoreTake(OS_FDTableSem, portMAX_DELAY);
			OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfd = -1;
			OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfp = NULL;
			OS_FDTableEx[filedes].OSfd = -1;
			strcpy(OS_FDTable[filedes].Path, "\0");
			OS_FDTable[filedes].User = 0;
			OS_FDTable[filedes].IsValid = FALSE;
			xSemaphoreGive(OS_FDTableSem);

			return OS_FS_ERROR;
		} else {
			/* fill in the table before returning */
			xSemaphoreTake(OS_FDTableSem, portMAX_DELAY);
			OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfd = -1;
			OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfp = NULL;
			OS_FDTableEx[filedes].OSfd = -1;
			strcpy(OS_FDTable[filedes].Path, "\0");
			OS_FDTable[filedes].User = 0;
			OS_FDTable[filedes].IsValid = FALSE;
			xSemaphoreGive(OS_FDTableSem);

			return OS_FS_SUCCESS;
		}
	} else if (volume_type == FS_BASED) {
		status = fclose(OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfp);
		if (status != 0) {
			/*
			 ** Remove the file from the OSAL list
			 ** to free up that slot
			 */
			/* fill in the table before returning */
			xSemaphoreTake(OS_FDTableSem, portMAX_DELAY);
			OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfd = -1;
			OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfp = NULL;
			OS_FDTableEx[filedes].OSfd = -1;
			strcpy(OS_FDTable[filedes].Path, "\0");
			OS_FDTable[filedes].User = 0;
			OS_FDTable[filedes].IsValid = FALSE;
			xSemaphoreGive(OS_FDTableSem);

			return OS_FS_ERROR;
		} else {
			/* fill in the table before returning */
			xSemaphoreTake(OS_FDTableSem, portMAX_DELAY);
			OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfd = -1;
			OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfp = NULL;
			OS_FDTableEx[filedes].OSfd = -1;
			strcpy(OS_FDTable[filedes].Path, "\0");
			OS_FDTable[filedes].User = 0;
			OS_FDTable[filedes].IsValid = FALSE;
			xSemaphoreGive(OS_FDTableSem);

			return OS_FS_SUCCESS;
		}
	} else {
		return OS_FS_ERR_PATH_INVALID;
	}

}/* end OS_close */

/*--------------------------------------------------------------------------------------
 Name: OS_read

 Purpose: reads up to nbytes from a file, and puts them into buffer.

 Returns: OS_FS_ERR_INVALID_POINTER if buffer is a null pointer
 OS_FS_ERROR if OS call failed
 OS_FS_ERR_INVALID_FD if the file descriptor passed in is invalid
 number of bytes read if success
 ---------------------------------------------------------------------------------------*/
int32 OS_read(uint32 filedes, void *buffer, uint32 nbytes) {
	size_t status;
	int32 volume_type;

	volume_type = OS_GetVolumeType(OS_FDTable[filedes].Path);

	if (buffer == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	/* Make sure the file descriptor is legit before using it */
	if (filedes
			< 0|| filedes >= OS_MAX_NUM_OPEN_FILES || OS_FDTable[filedes].IsValid == FALSE) {
		return OS_FS_ERR_INVALID_FD;
	} else if (volume_type == RAM_DISK) {
		status = ff_fread(buffer, 1, nbytes,
				OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfp);

		if (status <= 0) {
			return OS_FS_ERROR;
		}
	} else if (volume_type == FS_BASED) {
		status = fread(buffer, 1, nbytes,
				OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfp);

		if (status <= 0) {
			return OS_FS_ERROR;
		}
	} else {
		return OS_FS_ERR_PATH_INVALID;
	}

	return status;
}/* end OS_read */

/*--------------------------------------------------------------------------------------
 Name: OS_write

 Purpose: writes to a file. copies up to a maximum of nbtyes of buffer to the file
 described in filedes

 Returns: OS_FS_ERR_INVALID_POINTER if buffer is NULL
 OS_FS_ERROR if OS call failed
 OS_FS_ERR_INVALID_FD if the file descriptor passed in is invalid
 number of bytes written if success
 ---------------------------------------------------------------------------------------*/

int32 OS_write(uint32 filedes, const void *buffer, uint32 nbytes) {
	size_t status;
	int32 volume_type;

	volume_type = OS_GetVolumeType(OS_FDTable[filedes].Path);

	if (buffer == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	/* Make sure the file descriptor is legit before using it */
	if (filedes
			< 0|| filedes >= OS_MAX_NUM_OPEN_FILES || OS_FDTable[filedes].IsValid == FALSE) {
		return OS_FS_ERR_INVALID_FD;
	} else if (volume_type == RAM_DISK) {
		status = ff_fwrite(buffer, 1, nbytes,
				OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfp);

		if (status > 0) {
			return status;
		} else {
			return OS_FS_ERROR;
		}
	} else if (volume_type == FS_BASED) {
		status = fwrite(buffer, 1, nbytes,
				OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfp);

		if (status > 0) {
			return status;
		} else {
			return OS_FS_ERROR;
		}
	} else {
		return OS_FS_ERR_PATH_INVALID;
	}

}/* end OS_write */

/*--------------------------------------------------------------------------------------
 Name: OS_chmod

 Notes: This is not going to be implemented because there is no use for this function.
 ---------------------------------------------------------------------------------------*/

int32 OS_chmod(const char *path, uint32 access) {
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_FS_UNIMPLEMENTED;
} /* end OS_chmod */

/*--------------------------------------------------------------------------------------
 Name: OS_stat

 Purpose: returns information about a file or directory in a os_fs_stat structure

 Returns: OS_FS_ERR_INVALID_POINTER if path or filestats is NULL
 OS_FS_ERR_PATH_TOO_LONG if the path is too long to be stored locally
 *****        OS_FS_ERR_NAME_TOO_LONG if the name of the file is too long to be stored
 OS_FS_ERR_PATH_INVALID if path cannot be parsed
 OS_FS_ERROR id the OS call failed
 OS_FS_SUCCESS if success

 Note: The information returned is in the structure pointed to by filestats
 ---------------------------------------------------------------------------------------*/

int32 OS_stat(const char *path, os_fstat_t *filestats) {
	int ret_val;
	char local_path[OS_MAX_LOCAL_PATH_LEN];
	int32 volume_type;

	/*
	 ** Check to see if the file pointers are NULL
	 */
	if (path == NULL || filestats == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	/*
	 ** Check to see if the path is too long
	 */
	if (strlen(path) >= OS_MAX_PATH_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	/*
	 ** Translate the path
	 */
	if (OS_TranslatePath(path, (char *) local_path) != OS_FS_SUCCESS) {
		return OS_FS_ERR_PATH_INVALID;
	}

	volume_type = OS_GetVolumeType(path);

	if (volume_type == RAM_DISK) {
		FF_Stat_t xStatBuffer;
		ret_val = ff_stat(local_path, &xStatBuffer);
		if (ret_val != 0) {
			return OS_FS_ERROR;
		} else {
			filestats->FileModeBits = xStatBuffer.st_mode;
			filestats->FileSize = xStatBuffer.st_size;
#if( ffconfigTIME_SUPPORT == 1 )
			filestats->FileTime = xStatBuffer.st_ctime;
#else
			filestats->FileTime = 0;
#endif /* ffconfigTIME_SUPPORT */
			return OS_FS_SUCCESS;
		}
	} else if (volume_type == FS_BASED) {
		struct stat xStatBuffer;
		ret_val = stat(local_path, &xStatBuffer);
		if (ret_val != 0) {
			return OS_FS_ERROR;
		} else {
			filestats->FileModeBits = xStatBuffer.st_mode;
			filestats->FileSize = xStatBuffer.st_size;
#if( ffconfigTIME_SUPPORT == 1 )
			filestats->FileTime = xStatBuffer.st_ctime;
#else
			filestats->FileTime = 0;
#endif /* ffconfigTIME_SUPPORT */
			return OS_FS_SUCCESS;
		}
	} else {
		return OS_FS_ERR_PATH_INVALID;
	}
} /* end OS_stat */

/*--------------------------------------------------------------------------------------
 Name: OS_lseek

 Purpose: sets the read/write pointer to a specific offset in a specific file.
 Whence is either OS_SEEK_SET,OS_SEEK_CUR, or OS_SEEK_END

 Returns: the new offset from the beginning of the file
 OS_FS_ERR_INVALID_FD if the file descriptor passed in is invalid
 OS_FS_ERROR if OS call failed
 ---------------------------------------------------------------------------------------*/

int32 OS_lseek(uint32 filedes, int32 offset, uint32 whence) {
	off_t status;
	int where;
	int32 volume_type;

	volume_type = OS_GetVolumeType(OS_FDTable[filedes].Path);

	/* Make sure the file descriptor is legit before using it */
	if (filedes
			< 0|| filedes >= OS_MAX_NUM_OPEN_FILES || OS_FDTable[filedes].IsValid == FALSE) {
		return OS_FS_ERR_INVALID_FD;
	} else if (volume_type == RAM_DISK) {
		switch (whence) {
		case OS_SEEK_SET:
			where = SEEK_SET;
			break;
		case OS_SEEK_CUR:
			where = SEEK_CUR;
			break;
		case OS_SEEK_END:
			where = SEEK_END;
			break;
		default:
			return OS_FS_ERROR;
		}

		status = ff_fseek(OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfp,
				(off_t) offset, (int) where);

		if ((int) status == 0) {
			return (int32) ff_ftell(
					OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfp);
		} else {
			return OS_FS_ERROR;
		}
	} else if (volume_type == FS_BASED) {
		switch (whence) {
		case OS_SEEK_SET:
			where = SEEK_SET;
			break;
		case OS_SEEK_CUR:
			where = SEEK_CUR;
			break;
		case OS_SEEK_END:
			where = SEEK_END;
			break;
		default:
			return OS_FS_ERROR;
		}

		status = fseek(OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfp,
				(off_t) offset, (int) where);

		if ((int) status == 0) {
			return (int32) ff_ftell(
					OS_FDToFPMapping[OS_FDTableEx[filedes].OSfd].OSfp);
		} else {
			return OS_FS_ERROR;
		}
	} else {
		return OS_FS_ERR_PATH_INVALID;
	}
}/* end OS_lseek */

/*--------------------------------------------------------------------------------------
 Name: OS_remove

 Purpose: removes a given filename from the drive

 Returns: OS_FS_SUCCESS if the driver returns OK
 OS_FS_ERROR if there is no device or the driver returns error
 OS_FS_ERR_INVALID_POINTER if path is NULL
 OS_FS_ERR_PATH_TOO_LONG if path is too long to be stored locally
 OS_FS_ERR_PATH_INVALID if path cannot be parsed
 OS_FS_ERR_NAME_TOO_LONG if the name of the file to remove is too long to be
 stored locally
 ---------------------------------------------------------------------------------------*/

int32 OS_remove(const char *path) {
	int i;
	int status;
	char local_path[OS_MAX_LOCAL_PATH_LEN];
	int32 volume_type;

	/*
	 ** Check to see if the path pointer is NULL
	 */
	if (path == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	/*
	 ** Check to see if the path is too long
	 */
	if (strlen(path) >= OS_MAX_PATH_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	/*
	 ** check if the name of the file is too long
	 */
	if (OS_check_name_length(path) != OS_FS_SUCCESS) {
		return OS_FS_ERR_NAME_TOO_LONG;
	}

	/*
	 ** Make sure the file is not open by the OSAL before deleting it
	 */
	for (i = 0; i < OS_MAX_NUM_OPEN_FILES; i++) {
		if ((OS_FDTable[i].IsValid == TRUE)
				&& (strcmp(OS_FDTable[i].Path, path) == 0)) {
			return OS_FS_ERROR;
		}
	}

	/*
	 ** Translate the path
	 */
	if (OS_TranslatePath(path, (char *) local_path) != OS_FS_SUCCESS) {
		return OS_FS_ERR_PATH_INVALID;
	}

	volume_type = OS_GetVolumeType(path);

	/*
	 ** Call the system to remove the file
	 */
	if (volume_type == RAM_DISK) {
		status = ff_remove(local_path);
		if (status == 0) {
			return OS_FS_SUCCESS;
		} else {
			return OS_FS_ERROR;
		}
	} else if (volume_type == FS_BASED) {
		status = unlink(local_path);
		if (status == 0) {
			return OS_FS_SUCCESS;
		} else {
			return OS_FS_ERROR;
		}
	} else {
		return OS_FS_ERR_PATH_INVALID;
	}

} /* end OS_remove */

/*--------------------------------------------------------------------------------------
 Name: OS_rename

 Purpose: renames a file

 Returns: OS_FS_SUCCESS if the rename works
 OS_FS_ERROR if the file could not be opened or renamed.
 OS_FS_ERR_INVALID_POINTER if old_filename or new_filename are NULL
 OS_FS_ERR_PATH_INVALID if path cannot be parsed
 OS_FS_ERR_PATH_TOO_LONG if the paths given are too long to be stored locally
 OS_FS_ERR_NAME_TOO_LONG if the new name is too long to be stored locally
 ---------------------------------------------------------------------------------------*/

int32 OS_rename(const char *old_filename, const char *new_filename) {
	int status, i;
	char old_path[OS_MAX_LOCAL_PATH_LEN];
	char new_path[OS_MAX_LOCAL_PATH_LEN];
	int32 volume_type;

	/*
	 ** Check to see if the path pointers are NULL
	 */
	if (old_filename == NULL || new_filename == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	/*
	 ** Check to see if the paths are too long
	 */
	if (strlen(old_filename) >= OS_MAX_PATH_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	if (strlen(new_filename) >= OS_MAX_PATH_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	/*
	 ** check if the names of the files are too long
	 */
	if (OS_check_name_length(old_filename) != OS_FS_SUCCESS) {
		return OS_FS_ERR_NAME_TOO_LONG;
	}

	if (OS_check_name_length(new_filename) != OS_FS_SUCCESS) {
		return OS_FS_ERR_NAME_TOO_LONG;
	}

	/*
	 ** Translate the path
	 */
	if (OS_TranslatePath(old_filename, (char *) old_path) != OS_FS_SUCCESS) {
		return OS_FS_ERR_PATH_INVALID;
	}

	/*
	 ** Translate the path
	 */
	if (OS_TranslatePath(new_filename, (char *) new_path) != OS_FS_SUCCESS) {
		return OS_FS_ERR_PATH_INVALID;
	}

	volume_type = OS_GetVolumeType(old_filename);

	if (volume_type == RAM_DISK) {
		status = ff_rename(old_path, new_path, pdTRUE);
		if (status == 0) {
			for (i = 0; i < OS_MAX_NUM_OPEN_FILES; i++) {
				if (strcmp(OS_FDTable[i].Path, old_filename) == 0&&
				OS_FDTable[i].IsValid == TRUE) {
					strncpy(OS_FDTable[i].Path, new_filename, OS_MAX_PATH_LEN);
				}
			}
			return OS_FS_SUCCESS;
		} else {
			return OS_FS_ERROR;
		}
	} else if (volume_type == FS_BASED) {
		status = rename(old_path, new_path);
		if (status == 0) {
			for (i = 0; i < OS_MAX_NUM_OPEN_FILES; i++) {
				if (strcmp(OS_FDTable[i].Path, old_filename) == 0&&
				OS_FDTable[i].IsValid == TRUE) {
					strncpy(OS_FDTable[i].Path, new_filename, OS_MAX_PATH_LEN);
				}
			}
			return OS_FS_SUCCESS;
		} else {
			return OS_FS_ERROR;
		}
	} else {
		return OS_FS_ERR_PATH_INVALID;
	}
}/*end OS_rename */

/*--------------------------------------------------------------------------------------
 Name: OS_cp

 Purpose: Copies a single file from src to dest

 Returns: OS_FS_SUCCESS if the operation worked
 OS_FS_ERROR if the file could not be accessed
 OS_FS_ERR_INVALID_POINTER if src or dest are NULL
 OS_FS_ERR_PATH_INVALID if path cannot be parsed
 OS_FS_ERR_PATH_TOO_LONG if the paths given are too long to be stored locally
 OS_FS_ERR_NAME_TOO_LONG if the dest name is too long to be stored locally

 ---------------------------------------------------------------------------------------*/

int32 OS_cp(const char *src, const char *dest) {
	int i;
	char src_path[OS_MAX_LOCAL_PATH_LEN];
	char dest_path[OS_MAX_LOCAL_PATH_LEN];
	char data_buffer[512];
	int bytes_read;
	int bytes_written;
	int32 volume_type;

	/*
	 ** Check to see if the path pointers are NULL
	 */
	if (src == NULL || dest == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	/*
	 ** Check to see if the paths are too long
	 */
	if (strlen(src) >= OS_MAX_PATH_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	if (strlen(dest) >= OS_MAX_PATH_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	/*
	 ** check if the names of the files are too long
	 */
	if (OS_check_name_length(src) != OS_FS_SUCCESS) {
		return OS_FS_ERR_NAME_TOO_LONG;
	}

	if (OS_check_name_length(dest) != OS_FS_SUCCESS) {
		return OS_FS_ERR_NAME_TOO_LONG;
	}

	/*
	 ** Make sure the destintation file is not open by the OSAL before doing the copy
	 ** This may be caught by the host OS open call but it does not hurt to
	 ** be consistent
	 */
	for (i = 0; i < OS_MAX_NUM_OPEN_FILES; i++) {
		if ((OS_FDTable[i].IsValid == TRUE)
				&& (strcmp(OS_FDTable[i].Path, dest) == 0)) {
			return OS_FS_ERROR;
		}
	}

	/*
	 ** Translate the path
	 */
	if (OS_TranslatePath(src, (char *) src_path) != OS_FS_SUCCESS) {
		return OS_FS_ERR_PATH_INVALID;
	}

	/*
	 ** Translate the path
	 */
	if (OS_TranslatePath(dest, (char *) dest_path) != OS_FS_SUCCESS) {
		return OS_FS_ERR_PATH_INVALID;
	}

	volume_type = OS_GetVolumeType(src);

	if (volume_type == RAM_DISK) {
		FF_FILE *src_fd;
		FF_FILE *dest_fd;

		/*
		 ** Do the copy
		 */
		if ((src_fd = ff_fopen(src_path, "r")) == NULL) {
			return OS_FS_ERR_PATH_INVALID;
		}

		if ((dest_fd = ff_fopen(dest_path, "w")) == NULL) {
			ff_fclose(src_fd);
			return OS_FS_ERR_PATH_INVALID;
		}

		while ((bytes_read = ff_fread(data_buffer, 1, sizeof(data_buffer),
				src_fd)) > 0) {
			bytes_written = ff_fwrite(data_buffer, 1, bytes_read, dest_fd);
			if (bytes_written < 0) {
				ff_fclose(src_fd);
				ff_fclose(dest_fd);
				return OS_FS_ERROR;
			}
		}

		ff_fclose(src_fd);
		ff_fclose(dest_fd);

		if (bytes_read < 0) {
			return OS_FS_ERROR;
		} else {
			return OS_FS_SUCCESS;
		}
	} else if (volume_type == FS_BASED) {
		FILE *src_fp;
		FILE *dest_fp;

		/*
		 ** Do the copy
		 */
		if ((src_fp = fopen(src_path, "r")) == NULL) {
			return OS_FS_ERR_PATH_INVALID;
		}

		if ((dest_fp = fopen(dest_path, "w")) == NULL) {
			fclose(src_fp);
			return OS_FS_ERR_PATH_INVALID;
		}

		while ((bytes_read = fread(data_buffer, 1, sizeof(data_buffer), src_fp))
				> 0) {
			bytes_written = fwrite(data_buffer, 1, bytes_read, dest_fp);
			if (bytes_written < 0) {
				fclose(src_fp);
				fclose(dest_fp);
				return OS_FS_ERROR;
			}
		}

		fclose(src_fp);
		fclose(dest_fp);

		if (bytes_read < 0) {
			return OS_FS_ERROR;
		} else {
			return OS_FS_SUCCESS;
		}
	} else {
		return OS_FS_ERR_PATH_INVALID;
	}
}/*end OS_cp */

/*--------------------------------------------------------------------------------------
 Name: OS_mv

 Purpose: moves a single file from src to dest

 Returns: OS_FS_SUCCESS if the rename works
 OS_FS_ERROR if the file could not be opened or renamed.
 OS_FS_ERR_INVALID_POINTER if src or dest are NULL
 OS_FS_ERR_PATH_INVALID if path cannot be parsed
 OS_FS_ERR_PATH_TOO_LONG if the paths given are too long to be stored locally
 OS_FS_ERR_NAME_TOO_LONG if the dest name is too long to be stored locally

 ---------------------------------------------------------------------------------------*/

int32 OS_mv(const char *src, const char *dest) {
	int i;
	int32 status;

	/*
	 ** Validate the source and destination
	 ** These checks may seem redundant because OS_cp and OS_remove also do
	 ** the same checks, but this call needs to abort before doing a copy
	 ** in some cases.
	 */

	/*
	 ** Check to see if the path pointers are NULL
	 */
	if (src == NULL || dest == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	/*
	 ** Check to see if the paths are too long
	 */
	if (strlen(src) >= OS_MAX_PATH_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	if (strlen(dest) >= OS_MAX_PATH_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	/*
	 ** check if the names of the files are too long
	 */
	if (OS_check_name_length(src) != OS_FS_SUCCESS) {
		return OS_FS_ERR_NAME_TOO_LONG;
	}

	if (OS_check_name_length(dest) != OS_FS_SUCCESS) {
		return OS_FS_ERR_NAME_TOO_LONG;
	}

	/*
	 ** Make sure the source file is not open by the OSAL before doing the move
	 */
	for (i = 0; i < OS_MAX_NUM_OPEN_FILES; i++) {
		if ((OS_FDTable[i].IsValid == TRUE)
				&& (strcmp(OS_FDTable[i].Path, src) == 0)) {
			return OS_FS_ERROR;
		}
	}

	status = OS_cp(src, dest);
	if (status == OS_FS_SUCCESS) {
		status = OS_remove(src);
	}

	return (status);
}

/*
 ** Directory API
 */
/*--------------------------------------------------------------------------------------
 Name: OS_mkdir

 Purpose: makes a directory specified by path.

 Returns: OS_FS_ERR_INVALID_POINTER if path is NULL
 OS_FS_ERR_PATH_TOO_LONG if the path is too long to be stored locally
 OS_FS_ERR_PATH_INVALID if path cannot be parsed
 OS_FS_ERROR if the OS call fails
 OS_FS_SUCCESS if success

 Note: The access parameter is currently unused.
 ---------------------------------------------------------------------------------------*/

int32 OS_mkdir(const char *path, uint32 access) {
	int status;
	char local_path[OS_MAX_LOCAL_PATH_LEN];
	int32 volume_type;

	/*
	 ** Check to see if the path pointer is NULL
	 */
	if (path == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	/*
	 ** Check to see if the path is too long
	 */
	if (strlen(path) >= OS_MAX_PATH_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	/*
	 ** Translate the path
	 */
	if (OS_TranslatePath(path, (char *) local_path) != OS_FS_SUCCESS) {
		return OS_FS_ERR_PATH_INVALID;
	}

	volume_type = OS_GetVolumeType(path);

	if (volume_type == RAM_DISK) {
		status = ff_mkdir(local_path);

		if (status == 0) {
			return OS_FS_SUCCESS;
		} else {
			return OS_FS_ERROR;
		}
	} else if (volume_type == FS_BASED) {
		status = mkdir(local_path);

		if (status == 0) {
			return OS_FS_SUCCESS;
		} else {
			return OS_FS_ERROR;
		}
	} else {
		return OS_FS_ERR_PATH_INVALID;
	}
}/* end OS_mkdir */

int32 OS_DirectoryOpen(uint32 *dir_id, const char *path) {
	*dir_id = (int32)OS_opendir(path);
	if (dir_id == 0) {
		return OS_FS_ERROR;
	}
	else {
		return OS_FS_SUCCESS;
	}
}
/*--------------------------------------------------------------------------------------
 Name: OS_opendir

 Purpose: opens a directory for searching

 Returns: NULL if the directory cannot be opened
 Directory pointer if the directory is opened

 ---------------------------------------------------------------------------------------*/

void * OS_opendir(const char *path) {
	char local_path[OS_MAX_LOCAL_PATH_LEN];
	uint32 PossibleDir;
	int status;
	void * dirdescptr = NULL;
	int32 volume_type;

	/*
	 ** Check to see if the path pointer is NULL
	 */
	if (path == NULL) {
		return NULL;
	}

	/*
	 ** Check to see if the path is too long
	 */
	if (strlen(path) > OS_MAX_PATH_LEN) {
		return NULL;
	}

	/*
	 ** Translate the path
	 */
	if (OS_TranslatePath(path, (char *) local_path) != OS_FS_SUCCESS) {
		return NULL;
	}

	volume_type = OS_GetVolumeType(path);

	if (volume_type == RAM_DISK) {
		xSemaphoreTake(OS_DirTableSem, portMAX_DELAY);

		for (PossibleDir = 0; PossibleDir < OS_MAX_NUM_OPEN_FILES;
				PossibleDir++) {
			if (OS_DirTable[PossibleDir].IsValid == FALSE) {
				break;
			}
		}

		if (PossibleDir >= OS_MAX_NUM_OPEN_FILES) {
			xSemaphoreGive(OS_DirTableSem);
			return NULL;
		}

		/*
		 ** Mark the table entry as valid so no other
		 ** task can take that ID
		 */
		OS_DirTable[PossibleDir].IsValid = TRUE;

		status = ff_findfirst(local_path,
				&OS_DirTable[PossibleDir].dir.xFindData);
		if (status == 0) {
			strncpy(OS_DirTable[PossibleDir].Path, path, OS_MAX_PATH_LEN);
			OS_DirTable[PossibleDir].VolumeType = RAM_DISK;
			dirdescptr = (void *) &OS_DirTable[PossibleDir].dir.xFindData;
			OS_DirTable[PossibleDir].state = DirTableEntryStateAfterFindFirst;
		} else {
			strcpy(OS_DirTable[PossibleDir].Path, "\0");
			OS_DirTable[PossibleDir].IsValid = FALSE;
			OS_DirTable[PossibleDir].state = DirTableEntryStateUndefined;
			xSemaphoreGive(OS_DirTableSem);
			return NULL;
		}

		xSemaphoreGive(OS_DirTableSem);
	} else if (volume_type == FS_BASED) {
		xSemaphoreTake(OS_DirTableSem, portMAX_DELAY);

		for (PossibleDir = 0; PossibleDir < OS_MAX_NUM_OPEN_FILES;
				PossibleDir++) {
			if (OS_DirTable[PossibleDir].IsValid == FALSE) {
				break;
			}
		}

		if (PossibleDir >= OS_MAX_NUM_OPEN_FILES) {
			xSemaphoreGive(OS_DirTableSem);
			return NULL;
		}

		/*
		 ** Mark the table entry as valid so no other
		 ** task can take that ID
		 */
		OS_DirTable[PossibleDir].IsValid = TRUE;

		dirdescptr = opendir((char*) local_path);
		if (dirdescptr != NULL) {
			strncpy(OS_DirTable[PossibleDir].Path, path, OS_MAX_PATH_LEN);
			OS_DirTable[PossibleDir].VolumeType = FS_BASED;
			OS_DirTable[PossibleDir].dir.dp = dirdescptr;
			OS_DirTable[PossibleDir].state = DirTableEntryStateAfterFindFirst;
		} else {
			strcpy(OS_DirTable[PossibleDir].Path, "\0");
			OS_DirTable[PossibleDir].IsValid = FALSE;
			OS_DirTable[PossibleDir].state = DirTableEntryStateUndefined;
			xSemaphoreGive(OS_DirTableSem);
			return NULL;
		}

		xSemaphoreGive(OS_DirTableSem);
	} else {
		return NULL;
	}

	/*
	 ** will return a dirptr or NULL
	 */
	return dirdescptr;
} /* end OS_opendir */

int32 OS_DirectoryClose(uint32 dir_id) {
	return OS_closedir((void *)dir_id);
}

/*--------------------------------------------------------------------------------------
 Name: OS_closedir

 Purpose: closes a directory

 Returns: OS_FS_SUCCESS if success
 OS_FS_ERROR if close failed
 ---------------------------------------------------------------------------------------*/

int32 OS_closedir(void * directory) {
	uint32 PossibleDir;

	if (directory == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	xSemaphoreTake(OS_DirTableSem, portMAX_DELAY);

	for (PossibleDir = 0; PossibleDir < OS_MAX_NUM_OPEN_FILES; PossibleDir++) {
		if (((OS_DirTable[PossibleDir].VolumeType == RAM_DISK)
				&& ((void *) (&OS_DirTable[PossibleDir].dir.xFindData)
						== directory))
				|| ((OS_DirTable[PossibleDir].VolumeType == FS_BASED)
						&& ((void *) (OS_DirTable[PossibleDir].dir.dp)
								== directory))) {
			break;
		}
	}

	if (PossibleDir >= OS_MAX_NUM_OPEN_FILES) {
		xSemaphoreGive(OS_DirTableSem);
		return OS_FS_ERROR;
	}

	if (OS_DirTable[PossibleDir].IsValid != TRUE) {
		xSemaphoreGive(OS_DirTableSem);
		return OS_FS_ERROR;
	}

	if (OS_DirTable[PossibleDir].VolumeType == RAM_DISK) {
		OS_DirTable[PossibleDir].IsValid = FALSE;
		OS_DirTable[PossibleDir].state = DirTableEntryStateUndefined;
		strcpy(OS_DirTable[PossibleDir].Path, "\0");
		OS_DirTable[PossibleDir].VolumeType = -1;
	} else if (OS_DirTable[PossibleDir].VolumeType == FS_BASED) {
		int status;
		status = closedir(OS_DirTable[PossibleDir].dir.dp);

		OS_DirTable[PossibleDir].IsValid = FALSE;
		OS_DirTable[PossibleDir].state = DirTableEntryStateUndefined;
		strcpy(OS_DirTable[PossibleDir].Path, "\0");
		OS_DirTable[PossibleDir].VolumeType = -1;

		if (status != 0) {
			xSemaphoreGive(OS_DirTableSem);
			return OS_FS_ERROR;
		}

	} else {
		xSemaphoreGive(OS_DirTableSem);
		return OS_FS_ERR_PATH_INVALID;
	}

	xSemaphoreGive(OS_DirTableSem);

	return OS_FS_SUCCESS;
} /* end OS_closedir */

int32 OS_DirectoryRead(uint32 dir_id, os_dirent_t *dirent) {
	os_dirent_t *result;
	result = OS_readdir((void *) dir_id);
	if (result == NULL) {
		return OS_FS_ERROR;
	}
	else {
		*dirent = *result;
		return OS_FS_SUCCESS;
	}
}

/*--------------------------------------------------------------------------------------
 Name: OS_readdir

 Purpose: obtains directory entry data for the next file from an open directory

 Returns: a pointer to the next entry for success
 NULL if error or end of directory is reached
 ---------------------------------------------------------------------------------------*/

os_dirent_t * OS_readdir(void * directory) {
	uint32 PossibleDir;
	int status;

	if (directory == NULL) {
		return NULL;
	}

	xSemaphoreTake(OS_DirTableSem, portMAX_DELAY);

	for (PossibleDir = 0; PossibleDir < OS_MAX_NUM_OPEN_FILES; PossibleDir++) {
		if (((OS_DirTable[PossibleDir].VolumeType == RAM_DISK)
				&& ((void *) (&OS_DirTable[PossibleDir].dir.xFindData)
						== directory))
				|| ((OS_DirTable[PossibleDir].VolumeType == FS_BASED)
						&& ((void *) (OS_DirTable[PossibleDir].dir.dp)
								== directory))) {
			break;
		}
	}

	if (PossibleDir >= OS_MAX_NUM_OPEN_FILES) {
		xSemaphoreGive(OS_DirTableSem);
		return NULL;
	}

	if (OS_DirTable[PossibleDir].VolumeType == RAM_DISK) {
		if (OS_DirTable[PossibleDir].IsValid != TRUE) {
			xSemaphoreGive(OS_DirTableSem);
			return NULL;
		}

		switch (OS_DirTable[PossibleDir].state) {
		case DirTableEntryStateAfterFindFirst:
			/* do nothing */
			break;

		case DirTableEntryStateAfterFindNext:
			/* read the next entry */
			status = ff_findnext(&OS_DirTable[PossibleDir].dir.xFindData);
			if (status != 0) {
				xSemaphoreGive(OS_DirTableSem);
				return NULL;
			}
			break;

		case DirTableEntryStateUndefined:
		default:
			xSemaphoreGive(OS_DirTableSem);
			return NULL;
		}

		OS_DirTable[PossibleDir].state = DirTableEntryStateAfterFindNext;
		strncpy(OS_DirTable[PossibleDir].entry.FileName,
				OS_DirTable[PossibleDir].dir.xFindData.pcFileName,
				OS_MAX_PATH_LEN);
		OS_DirTable[PossibleDir].entry.FileName[OS_MAX_PATH_LEN - 1] = '\0';

		xSemaphoreGive(OS_DirTableSem);

		/*
		 ** Will return dirptr or NULL
		 */
		return &OS_DirTable[PossibleDir].entry;
	} else if (OS_DirTable[PossibleDir].VolumeType == FS_BASED) {
		static os_dirent_t dirent;
		struct dirent* tempptr;
		tempptr = readdir(OS_DirTable[PossibleDir].dir.dp);
		xSemaphoreGive(OS_DirTableSem);

		strncpy(dirent.FileName, tempptr->d_name, FILENAME_MAX);
		dirent.FileName[FILENAME_MAX-1] = '\0';
		return &dirent;
	} else {
		xSemaphoreGive(OS_DirTableSem);
		return NULL;
	}
} /* end OS_readdir */

/*--------------------------------------------------------------------------------------
 Name: OS_rewinddir

 Purpose: Rewinds the directory pointer

 Returns: N/A
 ---------------------------------------------------------------------------------------*/

void OS_rewinddir(void * directory) {
	char path[OS_MAX_LOCAL_PATH_LEN];
	uint32 PossibleDir;

	if (directory == NULL) {
		return;
	}

	xSemaphoreTake(OS_DirTableSem, portMAX_DELAY);

	for (PossibleDir = 0; PossibleDir < OS_MAX_NUM_OPEN_FILES; PossibleDir++) {
		if (((OS_DirTable[PossibleDir].VolumeType == RAM_DISK)
				&& ((void *) (&OS_DirTable[PossibleDir].dir.xFindData)
						== directory))
				|| ((OS_DirTable[PossibleDir].VolumeType == FS_BASED)
						&& ((void *) (OS_DirTable[PossibleDir].dir.dp)
								== directory))) {
			break;
		}
	}

	if (PossibleDir >= OS_MAX_NUM_OPEN_FILES) {
		xSemaphoreGive(OS_DirTableSem);
		return;
	}

	if (OS_DirTable[PossibleDir].VolumeType == RAM_DISK) {
		if (OS_DirTable[PossibleDir].IsValid != TRUE) {
			xSemaphoreGive(OS_DirTableSem);
			return;
		}

		strncpy(path, OS_DirTable[PossibleDir].Path, OS_MAX_PATH_LEN);
		OS_DirTable[PossibleDir].Path[OS_MAX_PATH_LEN - 1] = '\0';

		xSemaphoreGive(OS_DirTableSem);

		if (OS_closedir(
				(void *) (&OS_DirTable[PossibleDir].dir.xFindData)) != OS_FS_SUCCESS) {
			return;
		}

		OS_opendir(path);
	} else if (OS_DirTable[PossibleDir].VolumeType == FS_BASED) {
		xSemaphoreGive(OS_DirTableSem);
		rewinddir(OS_DirTable[PossibleDir].dir.dp);
	} else {
		xSemaphoreGive(OS_DirTableSem);
		return;
	}
}
/*--------------------------------------------------------------------------------------
 Name: OS_rmdir

 Purpose: removes a directory from  the structure (must be an empty directory)

 Returns: OS_FS_ERR_INVALID_POINTER if path is NULL
 OS_FS_ERR_PATH_INVALID if path cannot be parsed
 OS_FS_ERR_PATH_TOO_LONG
 ---------------------------------------------------------------------------------------*/

int32 OS_rmdir(const char *path) {
	int status;
	char local_path[OS_MAX_LOCAL_PATH_LEN];
	int32 volume_type;

	/*
	 ** Check to see if the path pointer is NULL
	 */
	if (path == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	/*
	 ** Check to see if the path is too long
	 */
	if (strlen(path) >= OS_MAX_PATH_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	/*
	 ** Translate the path
	 */
	if (OS_TranslatePath(path, (char *) local_path) != OS_FS_SUCCESS) {
		return OS_FS_ERR_PATH_INVALID;
	}

	volume_type = OS_GetVolumeType(path);

	if (volume_type == RAM_DISK) {
		status = ff_rmdir(local_path);

		if (status == 0) {
			return OS_FS_SUCCESS;
		} else {
			return OS_FS_ERROR;
		}
	} else if (volume_type == FS_BASED) {
		status = rmdir(local_path);

		if (status == 0) {
			return OS_FS_SUCCESS;
		} else {
			return OS_FS_ERROR;
		}
	} else {
		return OS_FS_ERR_PATH_INVALID;
	}

}/* end OS_rmdir */

/* --------------------------------------------------------------------------------------
 Name: OS_check_name_length

 Purpose: Checks the length of the file name at the end of the path.

 Returns: OS_FS_ERROR if path is NULL, path is too long, there is no '/' in the path
 name, the name is too long
 OS_SUCCESS if success

 NOTE: This is only an internal function and is not intended for use by the user
 ---------------------------------------------------------------------------------------*/

int32 OS_check_name_length(const char *path) {
	char* name_ptr;

	if (path == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	if (strlen(path) > OS_MAX_PATH_LEN) {
		return OS_FS_ERROR;
	}

	/*
	 * All names passed into this function are REQUIRED to contain at
	 * least one directory separator. Find the last one.
	 */
	name_ptr = strrchr(path, '/');
	if (name_ptr == NULL) {
		return OS_FS_ERROR;
	}

	/*
	 * Advance the pointer past the directory separator so that it
	 * indicates the final component of the path.
	 */
	name_ptr++;

	/*
	 * Reject paths whose final component is too long.
	 */
	if (strlen(name_ptr) > OS_MAX_FILE_NAME) {
		return OS_FS_ERROR;
	}

	return OS_FS_SUCCESS;

}/* end OS_check_name_length */
/* --------------------------------------------------------------------------------------
 Name: OS_ShellOutputToFile

 Purpose: Takes a shell command in and writes the output of that command to the specified file

 Returns: OS_FS_ERROR if the command was not executed properly
 OS_FS_ERR_INVALID_FD if the file descriptor passed in is invalid
 OS_SUCCESS if success
 ---------------------------------------------------------------------------------------*/
int32 OS_ShellOutputToFile(const char* Cmd, uint32 OS_fd) {
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_ERR_NOT_IMPLEMENTED;
}/* end OS_ShellOutputToFile */

/* --------------------------------------------------------------------------------------
 Name: OS_FDGetInfo

 Purpose: Copies the information of the given file descriptor into a structure passed in

 Returns: OS_FS_ERR_INVALID_FD if the file descriptor passed in is invalid
 OS_FS_SUCCESS if the copying was successfull
 ---------------------------------------------------------------------------------------*/

int32 OS_FDGetInfo(uint32 filedes, OS_file_prop_t *fd_prop) {
	if (fd_prop == NULL) {
		return (OS_FS_ERR_INVALID_POINTER);
	}

	/* Make sure the file descriptor is legit before using it */
	if (filedes
			< 0|| filedes >= OS_MAX_NUM_OPEN_FILES || OS_FDTable[filedes].IsValid == FALSE) {
		(*(fd_prop)).IsValid = FALSE;
		return OS_FS_ERR_INVALID_FD;
	} else {
		*fd_prop = OS_FDTable[filedes];
		return OS_FS_SUCCESS;
	}

}/* end OS_FDGetInfo */

/* --------------------------------------------------------------------------------------
 Name: OS_FileOpenCheck

 Purpose: Checks to see if a file is open

 Returns: OS_FS_ERROR if the file is not open
 OS_FS_SUCCESS if the file is open
 ---------------------------------------------------------------------------------------*/
int32 OS_FileOpenCheck(const char *Filename) {
	uint32 i;

	if (Filename == NULL) {
		return (OS_FS_ERR_INVALID_POINTER);
	}

	xSemaphoreTake(OS_FDTableSem, portMAX_DELAY);

	for (i = 0; i < OS_MAX_NUM_OPEN_FILES; i++) {
		if ((OS_FDTable[i].IsValid == TRUE)
				&& (strcmp(OS_FDTable[i].Path, Filename) == 0)) {
			xSemaphoreGive(OS_FDTableSem);
			return (OS_FS_SUCCESS);
		}
	}/* end for */

	xSemaphoreGive(OS_FDTableSem);
	return OS_FS_ERROR;
}/* end OS_FileOpenCheck */

/* --------------------------------------------------------------------------------------
 Name: OS_CloseFileByName

 Purpose: Allows a file to be closed by name.
 This will only work if the name passed in is the same name used to open
 the file.

 Returns: OS_FS_ERR_PATH_INVALID if the file is not found
 OS_FS_ERROR   if the file close returned an error
 OS_FS_SUCCESS if the file close suceeded
 ---------------------------------------------------------------------------------------*/
int32 OS_CloseFileByName(const char *Filename) {
	uint32 i;
	int status;
	int32 volume_type;

	if (Filename == NULL) {
		return (OS_FS_ERR_INVALID_POINTER);
	}

	volume_type = OS_GetVolumeType(Filename);

	for (i = 0; i < OS_MAX_NUM_OPEN_FILES; i++) {
		if ((OS_FDTable[i].IsValid == TRUE)
				&& (strcmp(OS_FDTable[i].Path, Filename) == 0)) {
			/*
			 ** Close the file
			 */
			if (volume_type == RAM_DISK) {
				status = ff_fclose(OS_FDToFPMapping[OS_FDTableEx[i].OSfd].OSfp);
			} else if (volume_type == FS_BASED) {
				status = fclose(OS_FDToFPMapping[OS_FDTableEx[i].OSfd].OSfp);
			} else {
				return OS_FS_ERR_PATH_INVALID;
			}

			/*
			 ** Next, remove the file from the OSAL list
			 ** to free up that slot
			 */
			xSemaphoreTake(OS_FDTableSem, portMAX_DELAY);
			OS_FDToFPMapping[OS_FDTableEx[i].OSfd].OSfd = -1;
			OS_FDToFPMapping[OS_FDTableEx[i].OSfd].OSfp = NULL;
			OS_FDTableEx[i].OSfd = -1;
			strcpy(OS_FDTable[i].Path, "\0");
			OS_FDTable[i].User = 0;
			OS_FDTable[i].IsValid = FALSE;
			xSemaphoreGive(OS_FDTableSem);
			if (status != 0) {
				return (OS_FS_ERROR);
			} else {
				return (OS_FS_SUCCESS);
			}
		}

	}/* end for */

	return (OS_FS_ERR_PATH_INVALID);
}/* end OS_CloseFileByName */

/* --------------------------------------------------------------------------------------
 Name: OS_CloseAllFiles

 Purpose: Closes All open files that were opened through the OSAL

 Returns: OS_FS_ERROR   if one or more file close returned an error
 OS_FS_SUCCESS if the files were all closed without error
 ---------------------------------------------------------------------------------------*/
int32 OS_CloseAllFiles(void) {
	uint32 i;
	int32 return_status = OS_FS_SUCCESS;
	int status;
	int32 volume_type;

	for (i = 0; i < OS_MAX_NUM_OPEN_FILES; i++) {
		if (OS_FDTable[i].IsValid == TRUE) {
			volume_type = OS_GetVolumeType(OS_FDTable[i].Path);

			/*
			 ** Close the file
			 */
			if (volume_type == RAM_DISK) {
				status = ff_fclose(OS_FDToFPMapping[OS_FDTableEx[i].OSfd].OSfp);
			} else if (volume_type == FS_BASED) {
				status = fclose(OS_FDToFPMapping[OS_FDTableEx[i].OSfd].OSfp);
			} else {
				return OS_FS_ERR_PATH_INVALID;
			}

			/*
			 ** Next, remove the file from the OSAL list
			 ** to free up that slot
			 */
			xSemaphoreTake(OS_FDTableSem, portMAX_DELAY);
			OS_FDToFPMapping[OS_FDTableEx[i].OSfd].OSfd = -1;
			OS_FDToFPMapping[OS_FDTableEx[i].OSfd].OSfp = NULL;
			OS_FDTableEx[i].OSfd = -1;
			strcpy(OS_FDTable[i].Path, "\0");
			OS_FDTable[i].User = 0;
			OS_FDTable[i].IsValid = FALSE;
			xSemaphoreGive(OS_FDTableSem);
			if (status != 0) {
				return_status = OS_FS_ERROR;
			}
		}

	}/* end for */

	return (return_status);
}/* end OS_CloseAllFiles */

