/******************************************************************************
 ** File: osconfig.h
 **
 ** Purpose:
 **   This header file contains the OS API  configuration parameters.
 **
 ** Author:  J. Brandenburg
 **
 ** Notes:
 ** Based on src/bsp/pc-rtems/config/osconfig.h
 ** Set OS_MAX_API_NAME to match the value in FreeRTOSConfig.h
 **
 ******************************************************************************/

#ifndef _osconfig_
#define _osconfig_

#include "FreeRTOSConfig.h"
#include "FreeRTOSFATConfig.h"

/*
 ** Platform Configuration Parameters for the OS API
 */

/*
 * There is apparently no limit on these values in FreeRTOS
 * So these values are limits for use in the OSAL
 */
#define OS_MAX_TASKS                64
#define OS_MAX_QUEUES               64
#define OS_MAX_COUNT_SEMAPHORES     20
#define OS_MAX_BIN_SEMAPHORES       20
#define OS_MAX_MUTEXES              20

/*
 ** Maximum length for an absolute path name
 */
//#define OS_MAX_PATH_LEN     (ffconfigMAX_FILENAME - OS_FS_PHYS_NAME_LEN)
#define OS_MAX_PATH_LEN     (250 - 64)

/*
 ** Maximum length for a local or host path/filename.
 **   This parameter can consist of the OSAL filename/path +
 **   the host OS physical volume name or path.
 */
#define OS_MAX_LOCAL_PATH_LEN (OS_MAX_PATH_LEN + OS_FS_PHYS_NAME_LEN)

/* 
 ** The maxium length allowed for a object (task,queue....) name by the underlying OS
 */
#define OS_MAX_API_NAME     configMAX_TASK_NAME_LEN

/*
 * Because the names used in cFS are often longer than expected by FreeRTOS
 * we use a "mapping" to shorter FreeRTOS object names.
 * The longest name we expect from cFS is a value we saw for other OSes
 */
#define MAX_API_NAME_INCOMING 20

/* 
 ** The maximum length for a file name
 */
#define OS_MAX_FILE_NAME    OS_MAX_PATH_LEN

/* 
 ** These defines are for OS_printf
 */
#define OS_BUFFER_SIZE 172
#define OS_BUFFER_MSG_DEPTH 100

/* This #define turns on a utility task that
 * will read the statements to print from
 * the OS_printf function. If you want OS_printf
 * to print the text out itself, comment this out 
 * 
 * NOTE: The Utility Task #defines only have meaning 
 * on the VxWorks operating systems
 */

#undef OS_UTILITY_TASK_ON

#ifdef OS_UTILITY_TASK_ON 
#define OS_UTILITYTASK_STACK_SIZE 2048
/* some room is left for other lower priority tasks */
#define OS_UTILITYTASK_PRIORITY   (configMAX_PRIORITIES - 5)
#endif

/* 
 ** the size of a command that can be passed to the underlying OS
 */
#define OS_MAX_CMD_LEN 1000

/*
 ** This define will include the OS network API.
 ** It should be turned off for targtets that do not have a network stack or
 ** device ( like the basic RAD750 vxWorks BSP )
 */
#undef OS_INCLUDE_NETWORK

/* 
 ** This is the maximum number of open file descriptors allowed at a time
 */
/*
 * FreeRTOS-FAT does not appear to limit the number of open files
 */
#define OS_MAX_NUM_OPEN_FILES 50 

/* 
 ** This defines the filethe input command of OS_ShellOutputToFile
 ** is written to in the VxWorks6 port
 */
#define OS_SHELL_CMD_INPUT_FILE_NAME "/ram/OS_ShellCmd.in"

/* 
 ** This define sets the queue implentation of the Linux port to use sockets
 ** commenting this out makes the Linux port use the POSIX message queues.
 */
/* #define OSAL_SOCKET_QUEUE */

/*
 ** Module loader/symbol table is optional
 */
#define OS_INCLUDE_MODULE_LOADER

#ifdef OS_INCLUDE_MODULE_LOADER
/*
 ** This define sets the size of the OS Module Table, which keeps track of the loaded modules in
 ** the running system. This define must be set high enough to support the maximum number of
 ** loadable modules in the system. If the the table is filled up at runtime, a new module load
 ** would fail.
 */
#define OS_MAX_MODULES 20

/*
 ** The Static Loader define is used for switching between the Dynamic and Static loader implementations.
 */
/* #define OS_STATIC_LOADER */

#endif

/*
 ** This define sets the maximum symbol name string length. It is used in implementations that
 ** support the symbols and symbol lookup.
 */
#define OS_MAX_SYM_LEN 64

/*
 ** This define sets the maximum number of time base objects
 ** The limit depends on the underlying OS and the resources it offers, but in general
 ** these are a limited resource and only a handful can be created.
 **
 ** This is included as an example, for OSAL implementations that do not [yet] support
 ** separate timebase objects, this directive will be ignored.  However, the OSAL unit
 ** test stub code does require that this is defined.
 */
#define OS_MAX_TIMEBASES      5

/*
 ** This define sets the maximum number of user timers available
 ** The limit here depends on whether the OSAL implementation uses limited resources
 ** for a timer object; in the case of the newer "posix-ng" and "rtems-ng" variants,
 ** the "timebase" allocates the OS resources and the timer does not use any additional
 ** OS resources. Therefore this limit can be higher.
 */
#define OS_MAX_TIMERS         5

/*
 ** This define sets the maximum number of open directories
 */
#define OS_MAX_NUM_OPEN_DIRS  4

/*
 ** This define sets the maximum depth of an OSAL message queue.  On some implementations this may
 ** affect the overall OSAL memory footprint so it may be beneficial to set this limit according to
 ** what the application actually needs.
 */
#define OS_QUEUE_MAX_DEPTH    50

/*
 * If OS_DEBUG_PRINTF is defined, this will enable the "OS_DEBUG" statements in the code
 * This should be left disabled in a normal build as it may affect real time performance as
 * well as producing extra console output.
 */
#undef OS_DEBUG_PRINTF

#endif