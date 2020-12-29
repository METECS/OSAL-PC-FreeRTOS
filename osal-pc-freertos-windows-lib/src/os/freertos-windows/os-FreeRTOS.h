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
 * \file   os-rtems.h
 * \author Christopher Sullivan based on work by joseph.p.hickey@nasa.gov and Jonathan Brandenburg
 *
 * Purpose: This file contains definitions that are shared across the FreeRTOS
 *          OSAL implementation.  This file is private to the FreeRTOS port and it
 *          may contain FreeRTOS-specific definitions.
 *
 */

/****************************************************************************************
 COMMON INCLUDE FILES
 ***************************************************************************************/

#include "FreeRTOS.h"
#include "semphr.h"
#include "common_types.h"
#include "osapi.h"
#include "os-impl.h"

#ifdef OS_INCLUDE_NETWORK
#include "FreeRTOS_sockets.h"
#endif

/****************************************************************************************
 DEFINES
 ***************************************************************************************/

/****************************************************************************************
 TYPEDEFS
 ***************************************************************************************/

typedef struct
{
	uint32    		ClockAccuracyNsec;
	TaskHandle_t	IdleTaskId;
} FreeRTOS_GlobalVars_t;

#ifdef OS_INCLUDE_NETWORK
typedef struct
{
	Socket_t socket;
    bool selectable;
} OS_FreeRTOS_socket_entry_t;
#endif

/****************************************************************************************
 GLOBAL DATA
 ***************************************************************************************/

extern FreeRTOS_GlobalVars_t FreeRTOS_GlobalVars;

#ifdef OS_INCLUDE_NETWORK
extern OS_FreeRTOS_socket_entry_t OS_impl_socket_table[OS_MAX_NUM_OPEN_FILES];
#endif

/****************************************************************************************
 FreeRTOS IMPLEMENTATION FUNCTION PROTOTYPES
 ***************************************************************************************/

int32 OS_FreeRTOS_TaskAPI_Impl_Init(void);
int32 OS_FreeRTOS_QueueAPI_Impl_Init(void);
int32 OS_FreeRTOS_BinSemAPI_Impl_Init(void);
int32 OS_FreeRTOS_CountSemAPI_Impl_Init(void);
int32 OS_FreeRTOS_MutexAPI_Impl_Init(void);
int32 OS_FreeRTOS_ModuleAPI_Impl_Init(void);
int32 OS_FreeRTOS_TimeBaseAPI_Impl_Init(void);
int32 OS_FreeRTOS_StreamAPI_Impl_Init(void);
int32 OS_FreeRTOS_DirAPI_Impl_Init(void);
int32 OS_FreeRTOS_FileSysAPI_Impl_Init(void);

int32 OS_GetVolumeType(const char *VirtualPath);

