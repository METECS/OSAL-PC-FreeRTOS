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
 * \file   osfileapi.c
 * \author Christopher Sullivan based on work by joseph.p.hickey@nasa.gov and Jonathan Brandenburg
 *
 * Purpose: This file Contains all of the api calls for manipulating
 *            files in a file system for FreeRTOS
 *
 */

/****************************************************************************************
 INCLUDE FILES
 ****************************************************************************************/

#include "os-FreeRTOS.h"
#include "ff_stdio.h"
#include "dirent.h"
#include "sys/stat.h"

/***************************************************************************************
 DATA TYPES
 ***************************************************************************************/

enum OS_DirTableEntryState
{
	DirTableEntryStateUndefined,
	DirTableEntryStateAfterFindFirst,
	DirTableEntryStateAfterFindNext
};

typedef struct
{
	int32 VolumeType;
	char Path[OS_MAX_PATH_LEN]; /* The path of the file opened */
	union
	{
		FF_FindData_t xFindData; /* The current state of the directory search */
		DIR *dp;
	} dir;
	enum OS_DirTableEntryState state;
} OS_DirTableEntry;

/***************************************************************************************
 External FUNCTION PROTOTYPES
 ***************************************************************************************/

/****************************************************************************************
 DEFINES
 ***************************************************************************************/

/****************************************************************************************
 GLOBAL DATA
 ****************************************************************************************/

/*
 * The file handle table.
 */
OS_FreeRTOS_filehandle_entry_t OS_impl_filehandle_table[OS_MAX_NUM_OPEN_FILES];

/*
 * The directory handle table.
 */
OS_DirTableEntry OS_impl_dir_table[OS_MAX_NUM_OPEN_DIRS];

/****************************************************************************************
 COMMON ROUTINES
 ****************************************************************************************/

/****************************************************************************************
 INITIALIZATION FUNCTION
 ****************************************************************************************/

/* --------------------------------------------------------------------------------------
    Name: OS_FreeRTOS_StreamAPI_Impl_Init

    Purpose: File/Stream subsystem global initialization

    Returns: OS_SUCCESS if success
 ---------------------------------------------------------------------------------------*/
int32 OS_FreeRTOS_StreamAPI_Impl_Init(void)
{
	uint32 i;

	for(i = 0; i < OS_MAX_NUM_OPEN_FILES; i++)
	{
		OS_impl_filehandle_table[i].VolumeType = -1;
		OS_impl_filehandle_table[i].fd = NULL;
		OS_impl_filehandle_table[i].selectable = false;
		OS_impl_filehandle_table[i].connected = false;
		OS_impl_filehandle_table[i].disconnected = false;
	}

	return OS_SUCCESS;
} /* end OS_FreeRTOS_StreamAPI_Impl_Init */

/* --------------------------------------------------------------------------------------
    Name: OS_FreeRTOS_DirAPI_Impl_Init

    Purpose: Directory table initialization

    Returns: OS_SUCCESS if success
 ---------------------------------------------------------------------------------------*/
int32 OS_FreeRTOS_DirAPI_Impl_Init(void)
{
	uint32 i;

	for(i = 0; i < OS_MAX_NUM_OPEN_DIRS; i++)
	{
		strcpy(OS_impl_dir_table[i].Path, "\0");
		OS_impl_dir_table[i].VolumeType = -1;
		OS_impl_dir_table[i].state = DirTableEntryStateUndefined;
	}

	return OS_SUCCESS;
} /* end OS_FreeRTOS_DirAPI_Impl_Init */

/* --------------------------------------------------------------------------------------
 Name: OS_ShellOutputToFile_Impl

 Purpose: Takes a shell command in and writes the output of that command to the specified file

 Returns: OS_FS_ERROR if the command was not executed properly
 OS_FS_ERR_INVALID_FD if the file descriptor passed in is invalid
 OS_SUCCESS if success
 ---------------------------------------------------------------------------------------*/
int32 OS_ShellOutputToFile_Impl(uint32 stream_id, const char* Cmd)
{
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_ERR_NOT_IMPLEMENTED;
}/* end OS_ShellOutputToFile_Impl */

/****************************************************************************************
 I/O API
 ***************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_GenericClose_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_GenericClose_Impl(uint32 local_id)
{
	int status = 0;

	//Network socket
	if(OS_impl_filehandle_table[local_id].selectable)
	{
		/* Initiate graceful shutdown. */
		status = FreeRTOS_shutdown(OS_impl_filehandle_table[local_id].fd, FREERTOS_SHUT_RDWR);

		if(status == 0) //TCP socket that is still connected
		{
			/* Wait for the socket to disconnect gracefully before closing the socket. */
			char Buf_rcv[100] = {0};
			TickType_t ticks = OS_Milli2Ticks(200);
			int result;
			while( (result = FreeRTOS_recv( OS_impl_filehandle_table[local_id].fd, Buf_rcv, sizeof(Buf_rcv), 0 )) >= 0)
			{
				if(result == 0)
				{
					vTaskDelay(ticks);
				}
			}
		}

		status = FreeRTOS_closesocket(OS_impl_filehandle_table[local_id].fd);
		if(status != 1)
		{
			return OS_FS_ERROR;
		}
		else
		{
			OS_impl_filehandle_table[local_id].VolumeType = -1;
			OS_impl_filehandle_table[local_id].fd = NULL;
			OS_impl_filehandle_table[local_id].selectable = false;
			OS_impl_filehandle_table[local_id].connected = false;
			OS_impl_filehandle_table[local_id].disconnected = false;

			return OS_FS_SUCCESS;
		}
	}
	//File
	else
	{
		if(OS_impl_filehandle_table[local_id].VolumeType == RAM_DISK)
		{
			status = ff_fclose(OS_impl_filehandle_table[local_id].fd);
		}
		else if(OS_impl_filehandle_table[local_id].VolumeType == FS_BASED)
		{
			status = fclose(OS_impl_filehandle_table[local_id].fd);
		}
		else
		{
			return OS_FS_ERR_PATH_INVALID;
		}

		if(status != 0)
		{
			return OS_FS_ERROR;
		}
		else
		{
			OS_impl_filehandle_table[local_id].VolumeType = -1;
			OS_impl_filehandle_table[local_id].fd = NULL;
			OS_impl_filehandle_table[local_id].selectable = false;
			OS_impl_filehandle_table[local_id].connected = false;
			OS_impl_filehandle_table[local_id].disconnected = false;
			return OS_FS_SUCCESS;
		}
	}
} /* end OS_GenericClose_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_GenericSeek_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_GenericSeek_Impl(uint32 local_id, int32 offset, uint32 whence)
{
	off_t status;
	int where;

	//Network socket
	if(OS_impl_filehandle_table[local_id].selectable)
	{
		return OS_FS_UNIMPLEMENTED;
	}
	//File
	else
	{
		switch(whence)
		{
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

		if(OS_impl_filehandle_table[local_id].VolumeType == RAM_DISK)
		{
			status = ff_fseek(OS_impl_filehandle_table[local_id].fd, (off_t) offset, (int) where);
		}
		else if(OS_impl_filehandle_table[local_id].VolumeType == FS_BASED)
		{
			status = fseek(OS_impl_filehandle_table[local_id].fd, (off_t) offset, (int) where);
		}
		else
		{
			return OS_FS_ERR_PATH_INVALID;
		}

		if((int) status == 0)
		{
			//Not sure if there should be an ftell option for FS_BASED. Old FreeRTOS code had it the way it currently is.
			return (int32) ff_ftell(OS_impl_filehandle_table[local_id].fd);
		}
		else
		{
			return OS_FS_ERROR;
		}
	}
} /* end OS_GenericSeek_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_GenericRead_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_GenericRead_Impl(uint32 local_id, void *buffer, uint32 nbytes, int32 timeout)
{
	int32 return_code;
	size_t status;
    uint32 operation;

	return_code = OS_SUCCESS;

	if(nbytes > 0)
	{
	   //Network socket
	   if(OS_impl_filehandle_table[local_id].selectable)
	   {
		   operation = OS_STREAM_STATE_READABLE;
		   return_code = OS_SelectSingle_Impl(local_id, &operation, timeout);
		   if (return_code == OS_SUCCESS && (operation & OS_STREAM_STATE_READABLE) != 0)
		   {
			   status = FreeRTOS_recv(OS_impl_filehandle_table[local_id].fd, buffer, nbytes, 0);
			   if(status == -pdFREERTOS_ERRNO_ENOTCONN)
			   {
				   return_code = 0; //This is the value BSD recv reports if the connection is closed.
			   }
			   else if(status < 0)
			   {
				   return_code = OS_ERROR;
			   }
			   else if(status == 0)
			   {
				   if(OS_impl_filehandle_table[local_id].disconnected)
				   {
					   return_code = 0; //This is the value BSD recv reports if the connection is closed.
				   }
				   else
				   {
					   return_code = OS_ERROR_TIMEOUT;
				   }
			   }
			   else
			   {
				   return_code = status;
			   }
		   }
	   }
	   //File
	   else
	   {
		   if(OS_impl_filehandle_table[local_id].VolumeType == RAM_DISK)
		   {
			   status = ff_fread(buffer, 1, nbytes, OS_impl_filehandle_table[local_id].fd);
		   }
		   else if(OS_impl_filehandle_table[local_id].VolumeType == FS_BASED)
		   {
			   status = fread(buffer, 1, nbytes, OS_impl_filehandle_table[local_id].fd);
		   }
		   else
		   {
			   return OS_FS_ERR_PATH_INVALID;
		   }

		   if(status <= 0)
		   {
			   return_code = OS_ERROR;
		   }
		   else
		   {
			   return_code = status;
		   }
	   }
	}

	return return_code;
} /* end OS_GenericRead_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_GenericWrite_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_GenericWrite_Impl(uint32 local_id, const void *buffer, uint32 nbytes, int32 timeout)
{
	size_t status;
	int32 return_code;
    uint32 operation;

	return_code = OS_SUCCESS;

	if(nbytes > 0)
	{
	   //Network socket
	   if(OS_impl_filehandle_table[local_id].selectable)
	   {
		   operation = OS_STREAM_STATE_WRITABLE;
		   return_code = OS_SelectSingle_Impl(local_id, &operation, timeout);
		   if (return_code == OS_SUCCESS && (operation & OS_STREAM_STATE_WRITABLE) != 0)
		   {
			   status = FreeRTOS_send(OS_impl_filehandle_table[local_id].fd, buffer, nbytes, 0);
			   if (status < 0)
			   {
				   return_code = OS_ERROR;
			   }
			   else
			   {
				   return_code = status;
			   }
		   }
	   }
	   //File
	   else
	   {
			if(OS_impl_filehandle_table[local_id].VolumeType == RAM_DISK)
			{
				status = ff_fwrite(buffer, 1, nbytes, OS_impl_filehandle_table[local_id].fd);
			}
			else if(OS_impl_filehandle_table[local_id].VolumeType == FS_BASED)
			{
				status = fwrite(buffer, 1, nbytes, OS_impl_filehandle_table[local_id].fd);
			}
			else
			{
				return OS_FS_ERR_PATH_INVALID;
			}

			if(status > 0)
			{
			   return_code = status;
			}
			else
			{
			   return_code = OS_ERROR;
			}
		}
	}

	return return_code;
} /* end OS_GenericWrite_Impl */

/****************************************************************************************
 Named File API
 ***************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_FileOpen_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_FileOpen_Impl(uint32 local_id, const char *local_path, int32 flags, int32 access)
{
	char *perm;
	int32 volume_type;

	/*
	 ** Check for a valid access mode
	 */
	switch(access)
	{
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

	volume_type = OS_GetVolumeType(local_path);

	if(volume_type == RAM_DISK)
	{
		OS_impl_filehandle_table[local_id].fd = ff_fopen(local_path, perm);
	}
	else if(volume_type == FS_BASED)
	{
		OS_impl_filehandle_table[local_id].fd = fopen(local_path, perm);
	}
	else
	{
		return OS_FS_ERR_PATH_INVALID;
	}

	if(OS_impl_filehandle_table[local_id].fd == NULL)
	{
		return OS_FS_ERROR;
	}

	OS_impl_filehandle_table[local_id].VolumeType = volume_type;

	return OS_FS_SUCCESS;
} /* end OS_FileOpen_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_FileStat_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_FileStat_Impl(const char *local_path, os_fstat_t *FileStats)
{
	int32 volume_type;
	int ret_val;

	volume_type = OS_GetVolumeType(local_path);

	if(volume_type == RAM_DISK)
	{
		FF_Stat_t xStatBuffer;
		ret_val = ff_stat(local_path, &xStatBuffer);

		if(ret_val != 0)
		{
			return OS_FS_ERROR;
		}
		FileStats->FileModeBits = xStatBuffer.st_mode;
		FileStats->FileSize = xStatBuffer.st_size;
#if( ffconfigTIME_SUPPORT == 1 )
		filestats->FileTime = xStatBuffer.st_ctime;
#else
		FileStats->FileTime = 0;
#endif /* ffconfigTIME_SUPPORT */
	}
	else if(volume_type == FS_BASED)
	{
		struct stat xStatBuffer;
		ret_val = stat(local_path, &xStatBuffer);

		if(ret_val != 0)
		{
			return OS_FS_ERROR;
		}
		FileStats->FileModeBits = xStatBuffer.st_mode;
		FileStats->FileSize = xStatBuffer.st_size;
#if( ffconfigTIME_SUPPORT == 1 )
		filestats->FileTime = xStatBuffer.st_ctime;
#else
		FileStats->FileTime = 0;
#endif /* ffconfigTIME_SUPPORT */
	}
	else
	{
		return OS_FS_ERR_PATH_INVALID;
	}

	return OS_FS_SUCCESS;
} /* end OS_FileStat_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_FileChmod_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_FileChmod_Impl(const char *local_path, uint32 access)
{
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_FS_UNIMPLEMENTED;
} /* end OS_FileChmod_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_FileRemove_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_FileRemove_Impl(const char *local_path)
{
	int status;
	int32 volume_type;

	volume_type = OS_GetVolumeType(local_path);

	/*
	 ** Call the system to remove the file
	 */
	if(volume_type == RAM_DISK)
	{
		status = ff_remove(local_path);
	}
	else if(volume_type == FS_BASED)
	{
		status = unlink(local_path);
	}
	else
	{
		return OS_FS_ERR_PATH_INVALID;
	}

	if(status == 0)
	{
		return OS_SUCCESS;
	}
	else
	{
		return OS_ERROR;
	}
} /* end OS_FileRemove_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_FileRename_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_FileRename_Impl(const char *old_path, const char *new_path)
{
	int status;
	int32 volume_type;

	volume_type = OS_GetVolumeType(old_path);

	if(volume_type == RAM_DISK)
	{
		status = ff_rename(old_path, new_path, pdTRUE);
	}
	else if(volume_type == FS_BASED)
	{
		status = rename(old_path, new_path);
	}
	else
	{
		return OS_FS_ERR_PATH_INVALID;
	}

	if(status == 0)
	{
		//If this is a directory, there may be a need to update the file path for rewinds.
		return OS_FS_SUCCESS;
	}
	else
	{
		return OS_FS_ERROR;
	}
} /* end OS_FileRename_Impl */

/****************************************************************************************
 Directory API
 ***************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_DirCreate_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_DirCreate_Impl(const char *local_path, uint32 access)
{
    int status;
	int32 volume_type;

	volume_type = OS_GetVolumeType(local_path);

	if(volume_type == RAM_DISK)
	{
		status = ff_mkdir(local_path);
	}
	else if(volume_type == FS_BASED)
	{
		status = mkdir(local_path);
	}
	else
	{
		return OS_FS_ERR_PATH_INVALID;
	}

	if(status == 0)
	{
		return OS_FS_SUCCESS;
	}
	else
	{
		return OS_FS_ERROR;
	}
} /* end OS_DirCreate_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_DirOpen_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_DirOpen_Impl(uint32 local_id, const char *local_path)
{
	int status;
	int32 volume_type;

	volume_type = OS_GetVolumeType(local_path);

	if(volume_type == RAM_DISK)
	{
		status = ff_findfirst(local_path, &OS_impl_dir_table[local_id].dir.xFindData);
		if(status == 0)
		{
			OS_impl_dir_table[local_id].dir.dp = (void *) &OS_impl_dir_table[local_id].dir.xFindData;
		}
	}
	else if(volume_type == FS_BASED)
	{
		OS_impl_dir_table[local_id].dir.dp = opendir((char*) local_path);
	}
	else
	{
		return OS_FS_ERROR;
	}

	if(OS_impl_dir_table[local_id].dir.dp != NULL)
	{
		strncpy(OS_impl_dir_table[local_id].Path, local_path, OS_MAX_PATH_LEN);
		OS_impl_dir_table[local_id].VolumeType = volume_type;
		OS_impl_dir_table[local_id].state = DirTableEntryStateAfterFindFirst;
		return OS_FS_SUCCESS;
	}
	else
	{
		strcpy(OS_impl_dir_table[local_id].Path, "\0");
		OS_impl_dir_table[local_id].state = DirTableEntryStateUndefined;
		return OS_FS_ERROR;
	}
} /* end OS_DirOpen_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_DirClose_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_DirClose_Impl(uint32 local_id)
{
	if(OS_impl_dir_table[local_id].VolumeType == RAM_DISK)
	{
		//Nothing special to do here.
	}
	else if(OS_impl_dir_table[local_id].VolumeType == FS_BASED)
	{
		int status;
		status = closedir(OS_impl_dir_table[local_id].dir.dp);
		if(status != 0)
		{
			return OS_FS_ERROR;
		}
	}
	else
	{
		return OS_FS_ERR_PATH_INVALID;
	}

	strcpy(OS_impl_dir_table[local_id].Path, "\0");
	OS_impl_dir_table[local_id].dir.dp = NULL;
	OS_impl_dir_table[local_id].VolumeType = -1;
	OS_impl_dir_table[local_id].state = DirTableEntryStateUndefined;

	return OS_FS_SUCCESS;
} /* end OS_DirClose_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_DirRead_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_DirRead_Impl(uint32 local_id, os_dirent_t *dirent)
{
	int status;

	if(OS_impl_dir_table[local_id].VolumeType == RAM_DISK)
	{
		switch(OS_impl_dir_table[local_id].state)
		{
		case DirTableEntryStateAfterFindFirst:
			/* do nothing */
			break;

		case DirTableEntryStateAfterFindNext:
			/* read the next entry */
			status = ff_findnext(&OS_impl_dir_table[local_id].dir.xFindData);
			if(status != 0)
			{
				return OS_FS_ERROR;
			}
			break;

		case DirTableEntryStateUndefined:
		default:
			return OS_FS_ERROR;
		}

		OS_impl_dir_table[local_id].state = DirTableEntryStateAfterFindNext;
		strncpy(dirent->FileName, OS_impl_dir_table[local_id].dir.xFindData.pcFileName, OS_MAX_PATH_LEN);
	}
	else if(OS_impl_dir_table[local_id].VolumeType == FS_BASED)
	{
		struct dirent* de;
		de = readdir(OS_impl_dir_table[local_id].dir.dp);
		if(de == NULL)
		{
			return OS_FS_ERROR;
		}

		strncpy(dirent->FileName, de->d_name, OS_MAX_PATH_LEN);
	}
	else
	{
		return OS_FS_ERROR;
	}

	dirent->FileName[OS_MAX_PATH_LEN-1] = '\0';

	return OS_FS_SUCCESS;
} /* end OS_DirRead_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_DirRewind_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_DirRewind_Impl(uint32 local_id)
{
	if(OS_impl_dir_table[local_id].VolumeType == RAM_DISK)
	{
		char path[OS_MAX_LOCAL_PATH_LEN];
		strncpy(path, OS_impl_dir_table[local_id].Path, OS_MAX_PATH_LEN);

		if(OS_DirClose_Impl(local_id) != OS_FS_SUCCESS)
		{
			return OS_FS_ERROR;
		}

		//This may fail if there are multiple levels of directories and
		//higher directories get renamed without the lower directories
		//getting their path updated.
		if(OS_DirOpen_Impl(local_id, path) != OS_FS_SUCCESS)
		{
			return OS_FS_ERROR;
		}
	}
	else if(OS_impl_dir_table[local_id].VolumeType == FS_BASED)
	{
		rewinddir(OS_impl_dir_table[local_id].dir.dp);
	}
	else
	{
		return OS_FS_ERROR;
	}

	return OS_FS_SUCCESS;
} /* end OS_DirRewind_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_DirRemove_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_DirRemove_Impl(const char *local_path)
{
	int status;
	int32 volume_type;

	volume_type = OS_GetVolumeType(local_path);

	if(volume_type == RAM_DISK)
	{
		status = ff_rmdir(local_path);
	}
	else if(volume_type == FS_BASED)
	{
		status = rmdir(local_path);
	}
	else
	{
		return OS_FS_ERR_PATH_INVALID;
	}

	if(status == 0)
	{
		return OS_FS_SUCCESS;
	}
	else
	{
		return OS_FS_ERROR;
	}
} /* end OS_DirRemove_Impl */
