/*
 * osapi-os-filesys-ex.h
 *
 *  Created on: Jan 13, 2020
 *      Author: Jonathan Brandenburg
 */

#ifndef OS_FREERTOS_WINDOWS_OSAPI_OS_FILESYS_EX_H_
#define OS_FREERTOS_WINDOWS_OSAPI_OS_FILESYS_EX_H_

typedef struct
{
    int32   OSfd;                   /* The underlying OS's file descriptor */
}OS_file_prop_Ex_t;

int32 OS_FS_Init(void);

int32 OS_GetVolumeType(const char *VirtualPath);

#endif /* OS_FREERTOS_WINDOWS_OSAPI_OS_FILESYS_EX_H_ */
