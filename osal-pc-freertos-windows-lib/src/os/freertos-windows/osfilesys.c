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
 * \file   osfilesys.c
 * \author Christopher Sullivan based on work by joseph.p.hickey@nasa.gov and Jonathan Brandenburg
 *
 * Purpose: This file has the apis for all of the making
 *          and mounting type of calls for file systems
 */

/****************************************************************************************
 INCLUDE FILES
 ****************************************************************************************/

#include "os-FreeRTOS.h"
#include "ff_stdio.h"
#include "ff_ramdisk.h"
#include "osconfig.h"
#include "osapi-os-filesys.h"

/****************************************************************************************
 Data Types
 ****************************************************************************************/

/***************************************************************************************
 FUNCTION PROTOTYPES
 ***************************************************************************************/

/****************************************************************************************
 GLOBAL DATA
 ****************************************************************************************/

/*
 ** This is the volume table reference. It is defined in the BSP/startup code for the board
 */
extern OS_VolumeInfo_t OS_VolumeTable[NUM_TABLE_ENTRIES];

/****************************************************************************************
 Filesys API
 ***************************************************************************************/

/* --------------------------------------------------------------------------------------
    Name: OS_FreeRTOS_FileSysAPI_Impl_Init

    Purpose: Filesystem API global initialization

    Returns: OS_SUCCESS if success
 ---------------------------------------------------------------------------------------*/
int32 OS_FreeRTOS_FileSysAPI_Impl_Init(void)
{
    return OS_SUCCESS;
} /* end OS_FreeRTOS_FileSysAPI_Impl_Init */

/*----------------------------------------------------------------
 *
 * Function: OS_FileSysStartVolume_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_FileSysStartVolume_Impl(uint32 filesys_id)
{
	OS_filesys_internal_record_t  *local = &OS_filesys_table[filesys_id];
    int32  return_code = OS_ERR_NOT_IMPLEMENTED;
	FF_Disk_t *disk;
	int allocated_space = 0;

	/*
	 * Take action based on the type of volume
	 */
	switch(local->fstype)
	{
	case OS_FILESYS_TYPE_DEFAULT:
	{
		/*
		 * This "mount" type is basically not a mount at all,
		 * No new filesystem is created, just put the files in a
		 * directory under the root FS.
		 *
		 * This is basically a pass-thru/no-op mode for compatibility
		 * with FS_BASED entries in existing volume tables.
		 */
		return_code = OS_SUCCESS;
		break;
	}
	case OS_FILESYS_TYPE_VOLATILE_DISK:
	{
		if(local->address == 0)
		{
			local->address = (char *) pvPortMalloc(local->numblocks * 512);
			if(local->address == NULL)
			{
				return OS_FS_ERR_DRIVE_NOT_CREATED;
			}
			else
			{
				allocated_space = 1;
			}
		}

		/*
		 ** Create the RAM disk device
		 */
		disk = FF_RAMDiskInit(local->volume_name, (uint8_t *) local->address, local->numblocks, 1024); /* The 4th parameter must be a multiple of the block size */
		if(disk == NULL)
		{
			if(allocated_space)
			{
				vPortFree(local->address);
			}
			return_code = OS_FS_ERR_DRIVE_NOT_CREATED;
		}
		else
		{

			return_code = OS_SUCCESS;
		}
		break;
	}
	default:
	{
		/*
		 ** The VolumeType is something else that is not supported right now
		 */
		return OS_ERR_NOT_IMPLEMENTED;
		break;
	}
	}

	/*
	 * If the operation was generally successful but a (real) FS
	 * mount point was not supplied, then generate one now.
	 *
	 * The path will be simply /<VOLNAME>
	 */
	if(return_code == OS_SUCCESS && local->system_mountpt[0] == 0)
	{
		snprintf(local->system_mountpt, sizeof(local->system_mountpt), "/%s", local->volume_name);
	}

	return return_code;
} /* end OS_FileSysStartVolume_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_FileSysStopVolume_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_FileSysStopVolume_Impl(uint32 filesys_id)
{
    /*
     * This is a no-op.
     *
     * Volatile volumes are just directories created in the temp dir,
     * and this will not remove the directories just in case something
     * went wrong.
     *
     * If the volume is started again, the directory will be re-used.
     */
    return OS_SUCCESS;
} /* end OS_FileSysStopVolume_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_FileSysFormatVolume_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_FileSysFormatVolume_Impl(uint32 filesys_id)
{
	/*
	 * NOTE: FreeRTOS always formats the filesystem to FAT.
	 * For backward compatibility this call must return success.
	 */
	return OS_SUCCESS;
} /* end OS_FileSysFormatVolume_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_FileSysMountVolume_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_FileSysMountVolume_Impl(uint32 filesys_id)
{
	OS_filesys_internal_record_t  *local = &OS_filesys_table[filesys_id];

	/*
	 * For volatile filesystems (ramdisk) these were created within
     * a temp filesystem, so all that is needed is to ensure the
     * mount point exists.  For any other FS type, trigger an
     * error to indicate that it is not implemented in this OSAL.
     */
    if(local->fstype != OS_FILESYS_TYPE_VOLATILE_DISK)
    {
        /* the mount command is only allowed on a ram disk */
        return OS_ERR_NOT_IMPLEMENTED;
    }

	return OS_SUCCESS;
} /* end OS_FileSysMountVolume_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_FileSysUnmountVolume_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_FileSysUnmountVolume_Impl(uint32 filesys_id)
{
    /*
     * NOTE: Mounting/Unmounting on FreeRTOS is not implemented.
     * For backward compatibility this call must return success.
     *
     * This is a no-op.  The mount point that was created during
     * the mount process can stay for the next mount.
     */
    return OS_FS_SUCCESS;
} /* end OS_FileSysUnmountVolume_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_FileSysStatVolume_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_FileSysStatVolume_Impl(uint32 filesys_id, OS_statvfs_t *result)
{
	int32_t status;
	OS_filesys_internal_record_t  *local = &OS_filesys_table[filesys_id];

	status = ff_diskfree(local->system_mountpt, NULL);
	if(status == 0)
	{
	   return OS_FS_ERROR;
	}

	result->block_size = 511;
	result->blocks_free = status;
	result->total_blocks = status; //Writing free blocks here because unsure what is required

	return OS_FS_SUCCESS;
} /* end OS_FileSysStatVolume_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_FileSysCheckVolume_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_FileSysCheckVolume_Impl(uint32 filesys_id, bool repair)
{
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_ERR_NOT_IMPLEMENTED;
} /* end OS_FileSysCheckVolume_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_GetVolumeType
 *
 *  Purpose: Returns the volume type of a file based on its path
 *
 *-----------------------------------------------------------------*/
int32 OS_GetVolumeType(const char *LocalPath)
{
	char devname[OS_MAX_PATH_LEN];
	char filename[OS_MAX_PATH_LEN];
	int NumChars;
	int i = 0;

	/*
	 ** Check to see if the path pointers are NULL
	 */
	if(LocalPath == NULL)
	{
		return OS_FS_ERR_INVALID_POINTER;
	}

	/*
	 ** Check to see if the path is too long
	 */
	if(strlen(LocalPath) >= OS_MAX_PATH_LEN)
	{
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	/*
	 ** All valid physical device names must start with either a '/' or "./"
	 */
	if(LocalPath[0] != '/' && strncmp(LocalPath,"./",2) != 0)
	{
		return OS_FS_ERR_PATH_INVALID;
	}

	/*
	 ** Fill the file and device name to be sure they do not have garbage
	 */
	memset((void *) devname, 0, OS_MAX_PATH_LEN);
	memset((void *) filename, 0, OS_MAX_PATH_LEN);

	/*
	 ** We want to find the number of chars to where the second "/" is (if there is one).
	 ** Since we know the first one is in spot 0, we start looking at 1, and go until
	 ** we find it.
	 */
	if(LocalPath[0] == '/') //If starts with '/'
	{
		NumChars = 1;
	}
	else //If starts with "./"
	{
		NumChars = 2;
	}

	while((NumChars <= strlen(LocalPath)) && (LocalPath[NumChars] != '/'))
	{
		NumChars++;
	}

	/*
	 ** Don't let it overflow to cause a segfault when trying to get the highest level
	 ** directory
	 */
	if(NumChars > strlen(LocalPath))
	{
		NumChars = strlen(LocalPath);
	}

	/*
	 ** copy over only the part that is the device name
	 */
	strncpy(devname, LocalPath, NumChars);
	devname[NumChars] = '\0';

	/*
	 ** Copy everything after the devname as the path/filename
	 */
	snprintf(filename, OS_MAX_PATH_LEN, "%s", LocalPath + NumChars);

	/*
	 ** look for the dev name we found in the VolumeTable
	 */
	for(i = 0; i < NUM_TABLE_ENTRIES; i++)
	{
		if(strncmp(OS_VolumeTable[i].PhysDevName, devname, NumChars) == 0)
		{
			break;
		}
	}

	/*
	 ** Make sure we found a valid drive
	 */
	if(i >= NUM_TABLE_ENTRIES)
	{
		return OS_FS_ERR_PATH_INVALID;
	}

	return OS_VolumeTable[i].VolumeType;
} /* end OS_GetVolumeType */
