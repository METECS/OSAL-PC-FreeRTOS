/*
 * Purpose: This file has the apis for all of the making
 *          and mounting type of calls for file systems
 *
 * Based on src/os/rtems/osfilesys.c from the OSAL distribution wit the
 * following license:
 *      Copyright (c) 2004-2006, United States government as represented by the
 *      administrator of the National Aeronautics Space Administration.
 *      All rights reserved. This software was created at NASAs Goddard
 *      Space Flight Center pursuant to government contracts.
 *
 *      This is governed by the NASA Open Source Agreement and may be used,
 *      distributed and modified only pursuant to the terms of that agreement.
 */

/****************************************************************************************
 INCLUDE FILES
 ****************************************************************************************/
#include "ff_ramdisk.h"
#include "ff_stdio.h"

#include "common_types.h"
#include "osapi.h"
#include "osconfig.h"
#include "osapi-os-filesys.h"

#include "osapi-os-filesys-ex.h"

/****************************************************************************************
 DEFINES
 ****************************************************************************************/
#undef OS_DEBUG_PRINTF

/****************************************************************************************
 GLOBAL DATA
 ****************************************************************************************/

/*
 ** This is the volume table reference. It is defined in the BSP/startup code for the board
 */
extern OS_VolumeInfo_t OS_VolumeTable[NUM_TABLE_ENTRIES];

/*
 ** Fd Table
 */
extern OS_file_prop_t OS_FDTable[OS_MAX_NUM_OPEN_FILES];

/*
 ** A semaphore to guard the file system calls.
 */
extern SemaphoreHandle_t OS_VolumeTableSem;

/****************************************************************************************
 Filesys API
 ****************************************************************************************/
/*
 ** System Level API
 */

/*
 ** Create the RAM disk.
 ** This currently supports one RAM disk.
 */
/*---------------------------------------------------------------------------------------
 Name: OS_mkfs

 Purpose: Makes a RAM disk on the target with the FAT file system

 Returns: OS_FS_ERR_INVALID_POINTER if devname is NULL
 OS_FS_ERR_DRIVE_NOT_CREATED if the OS calls to create the the drive failed
 OS_FS_ERR_DEVICE_NOT_FREE if the volume table is full
 OS_FS_SUCCESS on creating the disk

 Note: if address == 0, then a malloc will be called to create the disk
 ---------------------------------------------------------------------------------------*/
int32 OS_mkfs(char *address, const char *devname, const char * volname, uint32 blocksize,
		uint32 numblocks) {
	int i;
	uint32 ReturnCode;
	FF_Disk_t *disk;
	int allocated_space = 0;

	/*
	 ** Check parameters
	 */
	if (blocksize != 512) { /* look in ff_ramdisk.c for the required block size, ramSECTOR_SIZE */
		return OS_FS_ERR_DRIVE_NOT_CREATED;
	}

	if (devname == NULL || volname == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	if (strlen(devname) >= OS_FS_DEV_NAME_LEN
			|| strlen(volname) >= OS_FS_VOL_NAME_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	if (address == 0) {
		address = (char *) pvPortMalloc(numblocks * 512);
		if (address == NULL) {
			return OS_FS_ERR_DRIVE_NOT_CREATED;
		} else {
			allocated_space = 1;
		}
	}

	/*
	 ** Lock
	 */
	xSemaphoreTake(OS_VolumeTableSem, portMAX_DELAY);

	/* find an open entry in the Volume Table */
	for (i = 0; i < NUM_TABLE_ENTRIES; i++) {
		if (OS_VolumeTable[i].FreeFlag == TRUE
				&& OS_VolumeTable[i].IsMounted == FALSE
				&& strcmp(OS_VolumeTable[i].DeviceName, devname) == 0)
			break;
	}

	if (i >= NUM_TABLE_ENTRIES) {
		xSemaphoreGive(OS_VolumeTableSem);
		return OS_FS_ERR_DEVICE_NOT_FREE;
	}

	/*
	 ** Create the RAM disk and format it with the FAT file system.
	 */
	if (OS_VolumeTable[i].VolumeType == RAM_DISK) {
		/*
		 ** Create the RAM disk device
		 */
		strcpy(OS_VolumeTable[i].VolumeName, volname);
		disk = FF_RAMDiskInit(OS_VolumeTable[i].VolumeName, (uint8_t *) address,
				numblocks, 1024); /* The 4th parameter must be a multiple of the block size */
		if (disk == NULL) {
			if (allocated_space) {
				vPortFree(address);
			}
			ReturnCode = OS_FS_ERR_DRIVE_NOT_CREATED;
		} else {
			/*
			 ** Success
			 */
			OS_VolumeTable[i].FreeFlag = FALSE;
			strcpy(OS_VolumeTable[i].VolumeName, volname);
			OS_VolumeTable[i].BlockSize = blocksize;
			ReturnCode = OS_FS_SUCCESS;
		}
	} else if (OS_VolumeTable[i].VolumeType == FS_BASED) {
		/*
		 ** FS_BASED will map the OSAL Volume to an already mounted host filesystem
		 */

		/*
		 ** Enter the info in the table
		 */
		OS_VolumeTable[i].FreeFlag = FALSE;
		strcpy(OS_VolumeTable[i].VolumeName, volname);
		OS_VolumeTable[i].BlockSize = blocksize;

		ReturnCode = OS_FS_SUCCESS;
	} else {
		/*
		 ** The VolumeType is something else that is not supported right now
		 */
		ReturnCode = OS_FS_ERROR;
	}

	/*
	 ** Unlock
	 */
	xSemaphoreGive(OS_VolumeTableSem);

	return ReturnCode;

} /* end OS_mkfs */

/*---------------------------------------------------------------------------------------
 Name: OS_rmfs

 Purpose: Removes a file system from the volume table.

 Returns: OS_FS_ERR_INVALID_POINTER if devname is NULL
 OS_FS_ERROR is the drive specified cannot be located
 OS_FS_SUCCESS on removing  the disk
 ---------------------------------------------------------------------------------------*/
int32 OS_rmfs(const char *devname) {
	int i;
	int32 ReturnCode;

	if (devname == NULL) {
		ReturnCode = OS_FS_ERR_INVALID_POINTER;
	} else if (strlen(devname) >= OS_FS_DEV_NAME_LEN) {
		ReturnCode = OS_FS_ERR_PATH_TOO_LONG;
	} else {
		/*
		 ** Lock
		 */
		xSemaphoreTake(OS_VolumeTableSem, portMAX_DELAY);

		/* find this entry in the Volume Table */
		for (i = 0; i < NUM_TABLE_ENTRIES; i++) {
			if (OS_VolumeTable[i].FreeFlag == FALSE
					&& OS_VolumeTable[i].IsMounted == FALSE
					&& strcmp(OS_VolumeTable[i].DeviceName, devname) == 0) {
				break;
			}
		}

		/* We can't find that entry in the table */
		if (i >= NUM_TABLE_ENTRIES) {
			ReturnCode = OS_FS_ERROR;
		} else {
			/* Free this entry in the table */
			OS_VolumeTable[i].FreeFlag = TRUE;

			/* desconstruction of the filesystem to come later */
			ReturnCode = OS_FS_SUCCESS;
		}
		/*
		 ** Unlock
		 */
		xSemaphoreGive(OS_VolumeTableSem);
	}
	return ReturnCode;
}/* end OS_rmfs */

/*---------------------------------------------------------------------------------------
 Name: OS_initfs

 Purpose: Inititalizes a file system on the target

 Returns: OS_FS_ERR_INVALID_POINTER if devname is NULL
 OS_FS_DRIVE_NOT_CREATED if the OS calls to create the the drive failed
 OS_FS_SUCCESS on creating the disk
 OS_FS_ERR_PATH_TOO_LONG if the name is too long
 OS_FS_ERR_DEVICE_NOT_FREE if the volume table is full

 ---------------------------------------------------------------------------------------*/
int32 OS_initfs(char *address, const char *devname, const char *volname, uint32 blocksize,
		uint32 numblocks) {
	/*
	 * From looking at the RTEMS implementation, OS_initfs does not
	 * format the filesystem. However, FreeRTOS always formats the filesystem
	 * to FAT. Thus, I'll choose to delegate to OS_mkfs
	 */
	return OS_mkfs(address, devname, volname, blocksize, numblocks);
}/* end OS_initfs */

/*--------------------------------------------------------------------------------------
 Name: OS_mount

 Purpose: mounts a drive.

 ---------------------------------------------------------------------------------------*/

int32 OS_mount(const char *devname, const char* mountpoint) {
	int i;

	/* Check parameters */
	if (devname == NULL || mountpoint == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	if (strlen(devname) >= OS_FS_DEV_NAME_LEN
			|| strlen(mountpoint) >= OS_MAX_PATH_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	/*
	 ** Lock
	 */
	xSemaphoreTake(OS_VolumeTableSem, portMAX_DELAY);

	/* find the device in the table */
	for (i = 0; i < NUM_TABLE_ENTRIES; i++) {
		if ((OS_VolumeTable[i].FreeFlag == FALSE)
				&& (OS_VolumeTable[i].IsMounted == FALSE)
				&& (strcmp(OS_VolumeTable[i].DeviceName, devname) == 0)) {
			break;
		}
	}

	/* Return an error if an un-mounted device was not found */
	if (i >= NUM_TABLE_ENTRIES) {
		xSemaphoreGive(OS_VolumeTableSem);
		return OS_FS_ERROR;
	}

	if (OS_VolumeTable[i].VolumeType == RAM_DISK) {
		/*
		 ** Mount the RAM Disk
		 */
		/* attach the mountpoint */
		strcpy(OS_VolumeTable[i].MountPoint, mountpoint);
		OS_VolumeTable[i].IsMounted = TRUE;
	} else if (OS_VolumeTable[i].VolumeType == FS_BASED) {
		/* attach the mountpoint */
		strcpy(OS_VolumeTable[i].MountPoint, mountpoint);
		OS_VolumeTable[i].IsMounted = TRUE;
	} else {
		/*
		 ** VolumeType is not supported right now
		 */
		xSemaphoreGive(OS_VolumeTableSem);
		return OS_FS_ERROR;
	}

	/*
	 ** Unlock
	 */
	xSemaphoreGive(OS_VolumeTableSem);

	return OS_FS_SUCCESS;

}/* end OS_mount */

/*--------------------------------------------------------------------------------------
 Name: OS_unmount

 Purpose: unmounts a drive. and therefore makes all file descriptors pointing into
 the drive obsolete.

 Returns: OS_FS_ERR_INVALID_POINTER if name is NULL
 OS_FS_ERR_PATH_TOO_LONG if the absolute path given is too long
 OS_FS_ERROR if the OS calls failed
 OS_FS_SUCCESS if success
 ---------------------------------------------------------------------------------------*/
int32 OS_unmount(const char *mountpoint) {
	char local_path[OS_MAX_LOCAL_PATH_LEN];
	int i;

	if (mountpoint == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	if (strlen(mountpoint) >= OS_MAX_PATH_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	OS_TranslatePath(mountpoint, (char *) local_path);

	/*
	 ** Lock
	 */
	xSemaphoreTake(OS_VolumeTableSem, portMAX_DELAY);

	for (i = 0; i < NUM_TABLE_ENTRIES; i++) {
		if (OS_VolumeTable[i].FreeFlag == FALSE
				&& OS_VolumeTable[i].IsMounted == TRUE
				&& strcmp(OS_VolumeTable[i].MountPoint, mountpoint) == 0)
			break;
	}

	/* make sure we found the device */
	if (i >= NUM_TABLE_ENTRIES) {
#ifdef OS_DEBUG_PRINTF
		printf("OSAL: Error: unmount of %s failed: invalid volume table entry.\n",
				local_path);
#endif
		xSemaphoreGive(OS_VolumeTableSem);
		return OS_FS_ERROR;
	}

	if (OS_VolumeTable[i].VolumeType == RAM_DISK) {
		/*
		 ** Try to unmount the disk
		 */

		/* release the information from the table */
		OS_VolumeTable[i].IsMounted = FALSE;
		strcpy(OS_VolumeTable[i].MountPoint, "");

	} else if (OS_VolumeTable[i].VolumeType == FS_BASED) {

		/* release the information from the table */
		OS_VolumeTable[i].IsMounted = FALSE;
		strcpy(OS_VolumeTable[i].MountPoint, "");

	} else {
		/*
		 ** VolumeType is not supported right now
		 */
		xSemaphoreGive(OS_VolumeTableSem);
		return OS_FS_ERROR;
	}

	/*
	 ** Unlock
	 */
	xSemaphoreGive(OS_VolumeTableSem);
	return OS_FS_SUCCESS;
}/* end OS_umount */

/*--------------------------------------------------------------------------------------
 Name: OS_fsBlocksFree

 Purpose: Returns the number of free blocks in a volume

 Returns: OS_FS_ERR_INVALID_POINTER if name is NULL
 OS_FS_ERR_PATH_TOO_LONG if the name is too long
 OS_FS_ERROR if the OS call failed
 The number of blocks free in a volume if success
 ---------------------------------------------------------------------------------------*/
int32 OS_fsBlocksFree(const char *name) {
	int32_t status;
	char tmpFileName[OS_MAX_LOCAL_PATH_LEN + 1];
	int32 volume_type;

	if (name == NULL) {
		return (OS_FS_ERR_INVALID_POINTER);
	}

	/*
	 ** Check the length of the volume name
	 */
	if (strlen(name) >= OS_MAX_PATH_LEN) {
		return (OS_FS_ERR_PATH_TOO_LONG);
	}

	/*
	 ** Translate the path
	 */
	OS_TranslatePath(name, tmpFileName);

	/*
	 ** Lock
	 */
	xSemaphoreTake(OS_VolumeTableSem, portMAX_DELAY);

	volume_type = OS_GetVolumeType(name);

	if (volume_type == RAM_DISK) {
		status = ff_diskfree(tmpFileName, NULL);

		/*
		 ** Unlock
		 */
		xSemaphoreGive(OS_VolumeTableSem);

		if (status != 0) {
			return status;
		} else {
			return OS_FS_ERROR;
		}
	} else if (volume_type == FS_BASED) {
		/*
		 ** Unlock
		 */
		xSemaphoreGive(OS_VolumeTableSem);

		return OS_FS_UNIMPLEMENTED;

	} else {
		xSemaphoreGive(OS_VolumeTableSem);
		return OS_FS_ERR_PATH_INVALID;
	}

	/* should never reach this point */
	return OS_FS_ERROR;

}/* end OS_fsBlocksFree */

/*--------------------------------------------------------------------------------------
 Name: OS_fsBytesFree

 Purpose: Returns the number of free bytes in a volume

 Returns: OS_FS_ERR_INVALID_POINTER if name is NULL
 OS_FS_ERR_PATH_TOO_LONG if the name is too long
 OS_FS_ERROR if the OS call failed
 OS_FS_SUCCESS if the call succeeds
 ---------------------------------------------------------------------------------------*/
int32 OS_fsBytesFree(const char *name, uint64 *bytes_free) {
	int32_t status;
	uint64 bytes_free_local;
	char tmpFileName[OS_MAX_LOCAL_PATH_LEN + 1];

	if (name == NULL || bytes_free == NULL) {
		return (OS_FS_ERR_INVALID_POINTER);
	}

	/*
	 ** Check the length of the volume name
	 */
	if (strlen(name) >= OS_MAX_PATH_LEN) {
		return (OS_FS_ERR_PATH_TOO_LONG);
	}

	/*
	 ** Translate the path
	 */
	OS_TranslatePath(name, tmpFileName);

	/*
	 ** Lock
	 */
	xSemaphoreTake(OS_VolumeTableSem, portMAX_DELAY);

	status = ff_diskfree(tmpFileName, NULL);

	/*
	 ** Unlock
	 */
	xSemaphoreGive(OS_VolumeTableSem);

	if (status != 0) {
		bytes_free_local = status * 511;
		*bytes_free = bytes_free_local;
		return (OS_FS_SUCCESS);
	} else {
		return (OS_FS_ERROR);
	}
}/* end OS_fsBytesFree */

/*--------------------------------------------------------------------------------------
 Name: OS_chkfs

 Purpose: Checks the drives for inconsisenties and either repairs it or not

 Returns: OS_FS_ERR_INVALID_POINTER if name is NULL
 OS_FS_SUCCESS if success
 OS_FS_ERROR if the OS calls fail

 ---------------------------------------------------------------------------------------*/
int32 OS_chkfs(const char *name, bool repair) {
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_FS_UNIMPLEMENTED;

}/* end OS_chkfs */
/*--------------------------------------------------------------------------------------
 Name: OS_FS_GetPhysDriveName

 Purpose: Returns the name of the physical volume associated with the drive,
 when given the mount point of the drive

 Returns: OS_FS_ERR_INVALID_POINTER if either  parameter is NULL
 OS_SUCCESS if success
 OS_FS_ERROR if the mountpoint could not be found
 ---------------------------------------------------------------------------------------*/
int32 OS_FS_GetPhysDriveName(char * PhysDriveName, const char * MountPoint) {
	int32 ReturnCode;
	int i;

	if (MountPoint == NULL || PhysDriveName == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	if (strlen(PhysDriveName) >= OS_FS_DEV_NAME_LEN
			|| strlen(MountPoint) >= OS_MAX_PATH_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	/*
	 ** look for the CFS Mount Point in the VolumeTable
	 */
	for (i = 0; i < NUM_TABLE_ENTRIES; i++) {
		if (OS_VolumeTable[i].FreeFlag == FALSE
				&& strncmp(OS_VolumeTable[i].MountPoint, MountPoint,
				OS_MAX_PATH_LEN) == 0) {
			break;
		}
	}

	/*
	 ** Make sure we found a valid volume table entry
	 */
	if (i >= NUM_TABLE_ENTRIES) {
		ReturnCode = OS_FS_ERROR;
	} else {
		/*
		 ** Yes, so copy the physical drive name
		 */
		strncpy(PhysDriveName, OS_VolumeTable[i].PhysDevName,
		OS_FS_PHYS_NAME_LEN);
		ReturnCode = OS_SUCCESS;
	}

	return ReturnCode;
}/* end OS_FS_GetPhysDriveName */

/*-------------------------------------------------------------------------------------
 * Name: OS_TranslatePath
 * 
 * Purpose: Because of the abstraction of the filesystem across OSes, we have to change
 *          the name of the {file, directory, drive} to be what the OS can actually 
 *          accept
 ---------------------------------------------------------------------------------------*/
int32 OS_TranslatePath(const char *VirtualPath, char *LocalPath) {
	char devname[OS_MAX_PATH_LEN];
	char filename[OS_MAX_PATH_LEN];
	int NumChars;
	int i = 0;

	/*
	 ** Check to see if the path pointers are NULL
	 */
	if (VirtualPath == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	if (LocalPath == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	/*
	 ** Check to see if the path is too long
	 */
	if (strlen(VirtualPath) >= OS_MAX_PATH_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	/*
	 ** All valid Virtual paths must start with a '/' character
	 */
	if (VirtualPath[0] != '/') {
		return OS_FS_ERR_PATH_INVALID;
	}

	/*
	 ** Fill the file and device name to be sure they do not have garbage
	 */
	memset((void *) devname, 0, OS_MAX_PATH_LEN);
	memset((void *) filename, 0, OS_MAX_PATH_LEN);

	/*
	 ** We want to find the number of chars to where the second "/" is.
	 ** Since we know the first one is in spot 0, we start looking at 1, and go until
	 ** we find it.
	 */
	NumChars = 1;
	while ((NumChars <= strlen(VirtualPath)) && (VirtualPath[NumChars] != '/')) {
		NumChars++;
	}

	/*
	 ** Don't let it overflow to cause a segfault when trying to get the highest level
	 ** directory
	 */
	if (NumChars > strlen(VirtualPath)) {
		NumChars = strlen(VirtualPath);
	}

	/*
	 ** copy over only the part that is the device name
	 */
	strncpy(devname, VirtualPath, NumChars);
	devname[NumChars] = '\0';

	/*
	 ** Copy everything after the devname as the path/filename
	 */
	snprintf(filename, OS_MAX_PATH_LEN, "%s", VirtualPath + NumChars);

#ifdef OS_DEBUG_PRINTF
	printf("VirtualPath: %s, Length: %d\n",VirtualPath, (int)strlen(VirtualPath));
	printf("NumChars: %d\n",NumChars);
	printf("devname: %s\n",devname);
	printf("filename: %s\n",filename);
#endif

	/*
	 ** look for the dev name we found in the VolumeTable
	 */
	for (i = 0; i < NUM_TABLE_ENTRIES; i++) {
		if (OS_VolumeTable[i].FreeFlag == FALSE
				&& strncmp(OS_VolumeTable[i].MountPoint, devname, NumChars)
						== 0) {
			break;
		}
	}

	/*
	 ** Make sure we found a valid drive
	 */
	if (i >= NUM_TABLE_ENTRIES) {
		return OS_FS_ERR_PATH_INVALID;
	}

	/*
	 ** copy over the physical first part of the drive
	 */
	strncpy(LocalPath, OS_VolumeTable[i].PhysDevName, OS_MAX_LOCAL_PATH_LEN);
	NumChars = strlen(LocalPath);

	/*
	 ** Add the file name
	 */
	strncat(LocalPath, filename, (OS_MAX_LOCAL_PATH_LEN - NumChars));

#ifdef OS_DEBUG_PRINTF
	printf("Result of TranslatePath = %s\n",LocalPath);
#endif

	return OS_FS_SUCCESS;

} /* end OS_TranslatePath */

/*---------------------------------------------------------------------------------------
 Name: OS_FS_GetErrorName()

 Purpose: a handy debugging tool that will copy the name of the error code to a buffer

 Returns: OS_FS_ERROR if given error number is unknown
 OS_FS_SUCCESS if given error is found and copied to the buffer
 --------------------------------------------------------------------------------------- */
int32 OS_FS_GetErrorName(int32 error_num, os_fs_err_name_t * err_name) {
	/*
	 * Implementation note for developers:
	 *
	 * The size of the string literals below (including the terminating null)
	 * must fit into os_fs_err_name_t.  Always check the string length when
	 * adding or modifying strings in this function.  If changing
	 * os_fs_err_name_t then confirm these strings will fit.
	 */

	os_fs_err_name_t local_name;
	int32 return_code;

	return_code = OS_FS_SUCCESS;

	switch (error_num) {
	case OS_FS_SUCCESS:
		strcpy(local_name, "OS_FS_SUCCESS");
		break;
	case OS_FS_ERROR:
		strcpy(local_name, "OS_FS_ERROR");
		break;
	case OS_FS_ERR_INVALID_POINTER:
		strcpy(local_name, "OS_FS_ERR_INVALID_POINTER");
		break;
	case OS_FS_ERR_PATH_TOO_LONG:
		strcpy(local_name, "OS_FS_ERR_PATH_TOO_LONG");
		break;
	case OS_FS_ERR_NAME_TOO_LONG:
		strcpy(local_name, "OS_FS_ERR_NAME_TOO_LONG");
		break;
	case OS_FS_UNIMPLEMENTED:
		strcpy(local_name, "OS_FS_UNIMPLEMENTED");
		break;
	case OS_FS_ERR_PATH_INVALID:
		strcpy(local_name, "OS_FS_ERR_PATH_INVALID");
		break;
	case OS_FS_ERR_DRIVE_NOT_CREATED:
		strcpy(local_name, "OS_FS_ERR_DRIVE_NOT_CREATED");
		break;
	case OS_FS_ERR_NO_FREE_FDS:
		strcpy(local_name, "OS_FS_ERR_NO_FREE_FDS");
		break;
	case OS_FS_ERR_INVALID_FD:
		strcpy(local_name, "OS_FS_ERR_INVALID_FD");
		break;
	case OS_FS_ERR_DEVICE_NOT_FREE:
		strcpy(local_name, "OS_FS_DEVICE_NOT_FREE");
		break;
	default:
		strcpy(local_name, "ERROR_UNKNOWN");
		return_code = OS_FS_ERROR;
	}
	strcpy((char*) err_name, local_name);
	return return_code;
}

/*--------------------------------------------------------------------------------------
 Name: OS_GetFsInfo

 Purpose: returns information about the file system in an os_fsinfo_t

 Returns: OS_FS_ERR_INVALID_POINTER if filesys_info is NULL
 OS_FS_SUCCESS if success

 Note: The information returned is in the structure pointed to by filesys_info
 ---------------------------------------------------------------------------------------*/
int32 OS_GetFsInfo(os_fsinfo_t *filesys_info) {
	int i;

	/*
	 ** Check to see if the file pointers are NULL
	 */
	if (filesys_info == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	filesys_info->MaxFds = OS_MAX_NUM_OPEN_FILES;
	filesys_info->MaxVolumes = NUM_TABLE_ENTRIES;

	filesys_info->FreeFds = 0;
	for (i = 0; i < OS_MAX_NUM_OPEN_FILES; i++) {
		if (OS_FDTable[i].IsValid == FALSE) {
			filesys_info->FreeFds++;
		}

	}

	filesys_info->FreeVolumes = 0;
	for (i = 0; i < NUM_TABLE_ENTRIES; i++) {
		if (OS_VolumeTable[i].FreeFlag == TRUE) {
			filesys_info->FreeVolumes++;
		}
	}

	return (OS_FS_SUCCESS);
}

int32 OS_GetVolumeType(const char *VirtualPath) {
	char devname[OS_MAX_PATH_LEN];
	char filename[OS_MAX_PATH_LEN];
	int NumChars;
	int i = 0;

	/*
	 ** Check to see if the path pointers are NULL
	 */
	if (VirtualPath == NULL) {
		return OS_FS_ERR_INVALID_POINTER;
	}

	/*
	 ** Check to see if the path is too long
	 */
	if (strlen(VirtualPath) >= OS_MAX_PATH_LEN) {
		return OS_FS_ERR_PATH_TOO_LONG;
	}

	/*
	 ** All valid Virtual paths must start with a '/' character
	 */
	if (VirtualPath[0] != '/') {
		return OS_FS_ERR_PATH_INVALID;
	}

	/*
	 ** Fill the file and device name to be sure they do not have garbage
	 */
	memset((void *) devname, 0, OS_MAX_PATH_LEN);
	memset((void *) filename, 0, OS_MAX_PATH_LEN);

	/*
	 ** We want to find the number of chars to where the second "/" is.
	 ** Since we know the first one is in spot 0, we start looking at 1, and go until
	 ** we find it.
	 */
	NumChars = 1;
	while ((NumChars <= strlen(VirtualPath)) && (VirtualPath[NumChars] != '/')) {
		NumChars++;
	}

	/*
	 ** Don't let it overflow to cause a segfault when trying to get the highest level
	 ** directory
	 */
	if (NumChars > strlen(VirtualPath)) {
		NumChars = strlen(VirtualPath);
	}

	/*
	 ** copy over only the part that is the device name
	 */
	strncpy(devname, VirtualPath, NumChars);
	devname[NumChars] = '\0';

	/*
	 ** Copy everything after the devname as the path/filename
	 */
	snprintf(filename, OS_MAX_PATH_LEN, "%s", VirtualPath + NumChars);

#ifdef OS_DEBUG_PRINTF
	printf("VirtualPath: %s, Length: %d\n",VirtualPath, (int)strlen(VirtualPath));
	printf("NumChars: %d\n",NumChars);
	printf("devname: %s\n",devname);
	printf("filename: %s\n",filename);
#endif

	/*
	 ** look for the dev name we found in the VolumeTable
	 */
	for (i = 0; i < NUM_TABLE_ENTRIES; i++) {
		if (OS_VolumeTable[i].FreeFlag == FALSE
				&& strncmp(OS_VolumeTable[i].MountPoint, devname, NumChars)
						== 0) {
			break;
		}
	}

	/*
	 ** Make sure we found a valid drive
	 */
	if (i >= NUM_TABLE_ENTRIES) {
		return OS_FS_ERR_PATH_INVALID;
	}

	return OS_VolumeTable[i].VolumeType;

} /* end OS_TranslatePath */
