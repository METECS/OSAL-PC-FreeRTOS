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
 * \file   osapi.c
 * \author Christopher Sullivan based on work by joseph.p.hickey@nasa.gov and Jonathan Brandenburg
 *
 * Purpose:
 *      This file contains some of the OS APIs abstraction layer for FreeRTOS
 */

/****************************************************************************************
 INCLUDE FILES
 ****************************************************************************************/

#include "os-FreeRTOS.h"
#include <fcntl.h>
#ifndef configFREERTOS_RUN_AS_SIM
#error configFREERTOS_RUN_AS_SIM must be set to 0 or 1 in FreeRTOSConfig.h
#endif
#if configFREERTOS_RUN_AS_SIM == 1
#include "winbase.h"
#endif

/****************************************************************************************
 DEFINES
 ****************************************************************************************/

#define MAX_SEM_VALUE               0x7FFFFFFF

/*
 * By default use the stdout stream for the console (OS_printf)
 */
#define OSAL_CONSOLE_FILENO     STDOUT_FILENO

/*
 * By default the console output is always asynchronous
 * (equivalent to "OS_UTILITY_TASK_ON" being set)
 *
 * This option was removed from osconfig.h and now is
 * assumed to always be on.
 */
#define OS_CONSOLE_ASYNC                true
#define OS_CONSOLE_TASK_PRIORITY        OS_UTILITYTASK_PRIORITY
#define OS_CONSOLE_TASK_STACKSIZE       OS_UTILITYTASK_STACK_SIZE

/****************************************************************************************
 GLOBAL DATA
 ****************************************************************************************/

/*  tables for the properties of objects */

/*tasks */
typedef struct
{
	TaskHandle_t id;
} OS_impl_task_internal_record_t;

/* queues */
typedef struct
{
	QueueHandle_t id;
} OS_impl_queue_internal_record_t;

/* Binary Semaphores, Counting Semaphores, and Mutexes */
typedef struct
{
	SemaphoreHandle_t id;
} OS_impl_internal_record_t;

/* Console device */
typedef struct
{
    bool				is_async;
    SemaphoreHandle_t	data_sem;
    int					out_fd;
} OS_impl_console_internal_record_t;

/* Tables where the OS object information is stored */
OS_impl_task_internal_record_t		OS_impl_task_table[OS_MAX_TASKS];
OS_impl_queue_internal_record_t		OS_impl_queue_table[OS_MAX_QUEUES];
OS_impl_internal_record_t			OS_impl_bin_sem_table[OS_MAX_BIN_SEMAPHORES];
OS_impl_internal_record_t			OS_impl_count_sem_table[OS_MAX_COUNT_SEMAPHORES];
OS_impl_internal_record_t			OS_impl_mut_sem_table[OS_MAX_MUTEXES];
OS_impl_console_internal_record_t	OS_impl_console_table[OS_MAX_CONSOLES];

SemaphoreHandle_t	OS_task_table_sem;
SemaphoreHandle_t	OS_queue_table_sem;
SemaphoreHandle_t	OS_count_sem_table_sem;
SemaphoreHandle_t	OS_bin_sem_table_sem;
SemaphoreHandle_t	OS_mut_sem_table_sem;
SemaphoreHandle_t	OS_stream_table_mut;
SemaphoreHandle_t	OS_dir_table_mut;
SemaphoreHandle_t	OS_timebase_table_mut;
SemaphoreHandle_t	OS_module_table_mut;
SemaphoreHandle_t	OS_filesys_table_mut;
SemaphoreHandle_t	OS_console_mut;

static SemaphoreHandle_t * const MUTEX_TABLE[] =
      {
            [OS_OBJECT_TYPE_UNDEFINED] = NULL,
            [OS_OBJECT_TYPE_OS_TASK] = &OS_task_table_sem,
            [OS_OBJECT_TYPE_OS_QUEUE] = &OS_queue_table_sem,
            [OS_OBJECT_TYPE_OS_COUNTSEM] = &OS_count_sem_table_sem,
            [OS_OBJECT_TYPE_OS_BINSEM] = &OS_bin_sem_table_sem,
            [OS_OBJECT_TYPE_OS_MUTEX] = &OS_mut_sem_table_sem,
            [OS_OBJECT_TYPE_OS_STREAM] = &OS_stream_table_mut,
            [OS_OBJECT_TYPE_OS_DIR] = &OS_dir_table_mut,
            [OS_OBJECT_TYPE_OS_TIMEBASE] = &OS_timebase_table_mut,
            [OS_OBJECT_TYPE_OS_MODULE] = &OS_module_table_mut,
            [OS_OBJECT_TYPE_OS_FILESYS] = &OS_filesys_table_mut,
            [OS_OBJECT_TYPE_OS_CONSOLE] = &OS_console_mut,
      };

enum
{
   MUTEX_TABLE_SIZE = (sizeof(MUTEX_TABLE) / sizeof(MUTEX_TABLE[0]))
};

const OS_ErrorTable_Entry_t OS_IMPL_ERROR_NAME_TABLE[] = { { 0, NULL } };

/* A named pipe used to control the progress of the FreeRTOS application */
#ifndef configFREERTOS_RUN_AS_SIM
#error configFREERTOS_RUN_AS_SIM must be set to 0 or 1 in FreeRTOSConfig.h
#endif
#if configFREERTOS_RUN_AS_SIM == 1
HANDLE freertos_sync_pipe = INVALID_HANDLE_VALUE;
#endif

FreeRTOS_GlobalVars_t FreeRTOS_GlobalVars = { 0 };

/*----------------------------------------------------------------
 *
 * Function: OS_Lock_Global_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_Lock_Global_Impl(uint32 idtype)
{
	SemaphoreHandle_t *mut;

	if(idtype < MUTEX_TABLE_SIZE)
	{
		mut = MUTEX_TABLE[idtype];
	}
	else
	{
		mut = NULL;
	}

	if(mut == NULL)
	{
		return OS_ERROR;
	}

	if(xSemaphoreTake(*mut, portMAX_DELAY) != pdTRUE)
	{
			return OS_ERROR;
	}

	return OS_SUCCESS;
} /* end OS_Lock_Global_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_Unlock_Global_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_Unlock_Global_Impl(uint32 idtype)
{
	SemaphoreHandle_t *mut;

	if(idtype < MUTEX_TABLE_SIZE)
	{
		mut = MUTEX_TABLE[idtype];
	}
	else
	{
		mut = NULL;
	}

	if(mut == NULL)
	{
		return OS_ERROR;
	}

	if(xSemaphoreGive(*mut) != pdTRUE)
	{
		return OS_ERROR;
	}

	return OS_SUCCESS;
} /* end OS_Unlock_Global_Impl */

/****************************************************************************************
 INITIALIZATION FUNCTION
 ****************************************************************************************/

/*---------------------------------------------------------------------------------------
 Name: OS_API_Impl_Init

 Purpose: Initialize the tables that the OS API uses to keep track of information
 about objects

 returns: OS_SUCCESS or OS_ERROR
 ---------------------------------------------------------------------------------------*/
int32 OS_API_Impl_Init(uint32 idtype)
{
	int32 return_code = OS_SUCCESS;

	do
	{
	  /* Initialize the table mutex for the given idtype */
	  if(idtype < MUTEX_TABLE_SIZE && MUTEX_TABLE[idtype] != NULL)
	  {
		  *MUTEX_TABLE[idtype] = xSemaphoreCreateMutex();
		  if(*MUTEX_TABLE[idtype] == NULL)
		  {
			  return_code = OS_ERROR;
			  break;
		  }
	  }

	  switch(idtype)
	  {
	  case OS_OBJECT_TYPE_OS_TASK:
		 return_code = OS_FreeRTOS_TaskAPI_Impl_Init();
		 break;
	  case OS_OBJECT_TYPE_OS_QUEUE:
		 return_code = OS_FreeRTOS_QueueAPI_Impl_Init();
		 break;
	  case OS_OBJECT_TYPE_OS_BINSEM:
		 return_code = OS_FreeRTOS_BinSemAPI_Impl_Init();
		 break;
	  case OS_OBJECT_TYPE_OS_COUNTSEM:
		 return_code = OS_FreeRTOS_CountSemAPI_Impl_Init();
		 break;
	  case OS_OBJECT_TYPE_OS_MUTEX:
		 return_code = OS_FreeRTOS_MutexAPI_Impl_Init();
		 break;
	  case OS_OBJECT_TYPE_OS_MODULE:
		 return_code = OS_FreeRTOS_ModuleAPI_Impl_Init();
		 break;
	  case OS_OBJECT_TYPE_OS_TIMEBASE:
		 return_code = OS_FreeRTOS_TimeBaseAPI_Impl_Init();
		 break;
	  case OS_OBJECT_TYPE_OS_STREAM:
		 return_code = OS_FreeRTOS_StreamAPI_Impl_Init();
		 break;
	  case OS_OBJECT_TYPE_OS_DIR:
		 return_code = OS_FreeRTOS_DirAPI_Impl_Init();
		 break;
	  case OS_OBJECT_TYPE_OS_FILESYS:
		 return_code = OS_FreeRTOS_FileSysAPI_Impl_Init();
		 break;
	  default:
		 break;
	  }
   }
	while(0);

	//TODO: Determine if this is the correct way to initialize these.
	if(FreeRTOS_GlobalVars.initialized == 0)
	{
		if (return_code == OS_SUCCESS)
		{
			return_code = OS_FreeRTOS_NetworkAPI_Impl_Init();
		}

		if (return_code == OS_SUCCESS)
		{
			return_code = OS_FreeRTOS_SocketAPI_Impl_Init();
		}
		FreeRTOS_GlobalVars.initialized = 1;
	}

#ifndef configFREERTOS_RUN_AS_SIM
#error configFREERTOS_RUN_AS_SIM must be set to 0 or 1 in FreeRTOSConfig.h
#endif
#if configFREERTOS_RUN_AS_SIM == 1
	freertos_sync_pipe = CreateNamedPipeA(configFREERTOS_SYNC_PIPE_NAME,
			PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
			PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
			1, 16, 16, 0, NULL);
	if(freertos_sync_pipe == INVALID_HANDLE_VALUE) {
		return_code = OS_ERROR;
		return return_code;
	}
#endif

	return return_code;
} /* end OS_API_Impl_Init */

/*----------------------------------------------------------------
 *
 * Function: OS_IdleLoop_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
void OS_IdleLoop_Impl(void)
{
	FreeRTOS_GlobalVars.IdleTaskId = xTaskGetCurrentTaskHandle();
	vTaskSuspend(FreeRTOS_GlobalVars.IdleTaskId);
} /* end OS_IdleLoop_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_ApplicationShutdown_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
void OS_ApplicationShutdown_Impl(void)
{
	vTaskResume(FreeRTOS_GlobalVars.IdleTaskId);
} /* end OS_ApplicationShutdown_Impl */

/*---------------------------------------------------------------------------------------
   Name: OS_FreeRTOSEntry

   Purpose: A Simple FreeRTOS-compatible entry point that calls the common task entry function

   NOTES: This wrapper function is only used locally by OS_TaskCreate below

---------------------------------------------------------------------------------------*/
static void OS_FreeRTOSEntry(int arg)
{
    OS_TaskEntryPoint((uint32)arg);
} /* end OS_FreeRTOSEntry */

/****************************************************************************************
 TASK API
 ****************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_FreeRTOS_TaskAPI_Impl_Init
 *
 *  Purpose: Local helper routine, not part of OSAL API.
 *
 *-----------------------------------------------------------------*/
int32 OS_FreeRTOS_TaskAPI_Impl_Init(void)
{
	memset(OS_impl_task_table, 0, sizeof(OS_impl_task_table));

	return OS_SUCCESS;
} /* end OS_FreeRTOS_TaskAPI_Impl_Init */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskCreate_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TaskCreate_Impl(uint32 task_id, uint32 flags)
{
	BaseType_t status;

	/* Because all of cFS and OSAL have been written with the assumption that
	 * priorities range from 0 (highest priority) to 255 (lowest priority)
	 * Let's normalize that range into FreeRTOS priorities
	 */
	OS_task_table[task_id].priority = (255 - OS_task_table[task_id].priority) / (256/configMAX_PRIORITIES);

	status = xTaskCreate((TaskFunction_t) OS_FreeRTOSEntry,
			OS_task_table[task_id].task_name,
			OS_task_table[task_id].stack_size,
			(void *)OS_global_task_table[task_id].active_id,
			OS_task_table[task_id].priority,
			&OS_impl_task_table[task_id].id);

	if(status != pdPASS)
	{
		return OS_ERROR;
	}

	return OS_SUCCESS;
} /* end OS_TaskCreate_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskMatch_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TaskMatch_Impl(uint32 task_id)
{
	if(xTaskGetCurrentTaskHandle() != OS_impl_task_table[task_id].id)
	{
	   return OS_ERROR;
	}

   return OS_SUCCESS;
}/* end OS_TaskMatch_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskDelete_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TaskDelete_Impl(uint32 task_id)
{
	/*
	** Try to delete the task
	** If this fails, not much recourse - the only potential cause of failure
	** to cancel here is that the thread ID is invalid because it already exited itself,
	** and if that is true there is nothing wrong - everything is OK to continue normally.
	*/
	vTaskDelete(OS_impl_task_table[task_id].id);
	OS_impl_task_table[task_id].id = (TaskHandle_t)0xFFFF;

	return OS_SUCCESS;
}/* end OS_TaskDelete_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskExit_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
void OS_TaskExit_Impl(void)
{
	vTaskDelete(xTaskGetCurrentTaskHandle());
}/*end OS_TaskExit_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskDelay_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TaskDelay_Impl(uint32 milli_second)
{
	TickType_t ticks;

	ticks = OS_Milli2Ticks(milli_second);
	vTaskDelay(ticks);

	return OS_SUCCESS;
}/* end OS_TaskDelay_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskSetPriority_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TaskSetPriority_Impl(uint32 task_id, uint32 new_priority)
{
	/* Because all of cFS and OSAL have been written with the assumption that
	 * priorities range from 0 (highest priority) to 255 (lowest priority)
	 * Let's normalize that range into FreeRTOS priorities
	 */
	new_priority = (255 - new_priority) / (256/configMAX_PRIORITIES);

	/* Set Task Priority */
	vTaskPrioritySet(OS_impl_task_table[task_id].id, new_priority);

	return OS_SUCCESS;
}/* end OS_TaskSetPriority_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskRegister_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TaskRegister_Impl(uint32 global_task_id)
{
	TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();

	if(currentTask != NULL)
	{
		vTaskSetThreadLocalStoragePointer(currentTask,//current task handle
										  0,//index
										  (void *)global_task_id);//value to store
		return OS_SUCCESS;
	}
	else
	{
		return OS_ERROR;
	}
}/* end OS_TaskRegister_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskGetId_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
uint32 OS_TaskGetId_Impl(void)
{
	TaskHandle_t currentTask= xTaskGetCurrentTaskHandle();

	if(currentTask != NULL)
	{
		uint32 task_id;
		task_id = (uint32) pvTaskGetThreadLocalStoragePointer(currentTask, 0);
		return task_id;
	}
	else
	{
		return OS_ERR_NAME_NOT_FOUND;
	}
}/* end OS_TaskGetId_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskGetInfo_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TaskGetInfo_Impl(uint32 task_id, OS_task_prop_t *task_prop)
{
	task_prop->OStask_id = (uint32) OS_impl_task_table[task_id].id;

	return OS_SUCCESS;
} /* end OS_TaskGetInfo_Impl */

/****************************************************************************************
 MESSAGE QUEUE API
 ****************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_FreeRTOS_QueueAPI_Impl_Init
 *
 *  Purpose: Local helper routine, not part of OSAL API.
 *
 *-----------------------------------------------------------------*/
int32 OS_FreeRTOS_QueueAPI_Impl_Init(void)
{
    memset(OS_impl_queue_table, 0, sizeof(OS_impl_queue_table));

    return OS_SUCCESS;
} /* end OS_FreeRTOS_QueueAPI_Impl_Init */

/*----------------------------------------------------------------
 *
 * Function: OS_QueueCreate_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_QueueCreate_Impl(uint32 queue_id, uint32 flags)
{
	/*
	 ** Create the message queue.
	 */
	OS_impl_queue_table[queue_id].id = xQueueCreate(
			OS_queue_table[queue_id].max_depth,
			OS_queue_table[queue_id].max_size);

	/*
	 ** If the operation failed, report the error
	 */
	if(OS_impl_queue_table[queue_id].id == NULL)
	{
		OS_impl_queue_table[queue_id].id = 0;
		return OS_ERROR;
	}

	return OS_SUCCESS;
} /* end OS_QueueCreate_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_QueueDelete_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_QueueDelete_Impl(uint32 queue_id)
{
    /* Try to delete the queue */
    vQueueDelete(OS_impl_queue_table[queue_id].id);
    OS_impl_queue_table[queue_id].id = (SemaphoreHandle_t)0xFFFF;

    return OS_SUCCESS;
} /* end OS_QueueDelete_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_QueueGet_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_QueueGet_Impl(uint32 queue_id, void *data, uint32 size, uint32 *size_copied, int32 timeout)
{
	/* msecs rounded to the closest system tick count */
	BaseType_t status;
	TickType_t ticks;
	QueueHandle_t os_queue_id;

	os_queue_id = OS_impl_queue_table[queue_id].id;

	/* Get Message From Message Queue */
	if(timeout == OS_PEND)
	{
		/*
		 ** Pend forever until a message arrives.
		 */
		status = xQueueReceive(os_queue_id, data, portMAX_DELAY);
		if(status == pdTRUE)
		{
			*size_copied = OS_queue_table[queue_id].max_size;
		}
		else
		{
			*size_copied = 0;
		}
	}
	else if(timeout == OS_CHECK)
	{
		/*
		 ** Get a message without waiting.  If no message is present,
		 ** return with a failure indication.
		 */
		status = xQueueReceive(os_queue_id, data, 0);
		if(status == pdTRUE)
		{
			*size_copied = OS_queue_table[queue_id].max_size;
		}
		else
		{
			*size_copied = 0;
			return OS_QUEUE_EMPTY;
		}
	}
	else
	{
		/*
		 ** Wait for up to a specified amount of time for a message to arrive.
		 ** If no message arrives within the timeout interval, return with a
		 ** failure indication.
		 */
		ticks = OS_Milli2Ticks(timeout);

		status = xQueueReceive(os_queue_id, data, ticks);
		if(status == pdTRUE)
		{
			*size_copied = OS_queue_table[queue_id].max_size;
		}
		else
		{
			*size_copied = 0;
			return OS_QUEUE_TIMEOUT;
		}

	}/* else */

	/*
	 ** Check the status of the read operation.  If a valid message was
	 ** obtained, indicate success.
	 */
	if(status == pdTRUE)
	{
		/* Success. */
		return OS_SUCCESS;
	}
	else
	{
		*size_copied = 0;
		return OS_ERROR;
	}
}/* end OS_QueueGet_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_QueuePut_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_QueuePut_Impl(uint32 queue_id, const void *data, uint32 size, uint32 flags)
{
	BaseType_t status;
	QueueHandle_t os_queue_id;

	os_queue_id = OS_impl_queue_table[queue_id].id;

	/* Write the buffer pointer to the queue.  If an error occurred, report it
	 ** with the corresponding SB status code.
	 */
	status = xQueueSend(os_queue_id, data, 0);

	if(status == pdTRUE)
	{
		return OS_SUCCESS;
	}
	else if(status == errQUEUE_FULL)
	{
		/*
		 ** Queue is full.
		 */
		return OS_QUEUE_FULL;
	}
	else
	{
		/*
		 ** Unexpected error while writing to queue.
		 */
		return OS_ERROR;
	}
}/* end OS_QueuePut_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_QueueGetInfo_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_QueueGetInfo_Impl(uint32 queue_id, OS_queue_prop_t *queue_prop)
{
    return OS_SUCCESS;
} /* end OS_QueueGetInfo_Impl */

/****************************************************************************************
 BINARY SEMAPHORE API
 ***************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_FreeRTOS_BinSemAPI_Impl_Init
 *
 *  Purpose: Local helper routine, not part of OSAL API.
 *
 *-----------------------------------------------------------------*/
int32 OS_FreeRTOS_BinSemAPI_Impl_Init(void)
{
    memset(OS_impl_bin_sem_table, 0, sizeof(OS_impl_bin_sem_table));

    return OS_SUCCESS;
} /* end OS_FreeRTOS_BinSemAPI_Impl_Init */

/*----------------------------------------------------------------
 *
 * Function: OS_BinSemCreate_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_BinSemCreate_Impl(uint32 sem_id, uint32 sem_initial_value, uint32 options)
{
	/* Check to make sure the sem value is going to be either 0 or 1 */
	if(sem_initial_value > 1)
	{
		sem_initial_value = 1;
	}

	/* Create Semaphore */
	OS_impl_bin_sem_table[sem_id].id = xSemaphoreCreateBinary();

	/* check if Create failed */
	if(OS_impl_bin_sem_table[sem_id].id == NULL)
	{
		OS_impl_bin_sem_table[sem_id].id = 0;
		return OS_SEM_FAILURE;
	}

	// Initialize the semaphore value
	for(int i = 0; i < sem_initial_value; i++)
	{
		xSemaphoreGive(OS_impl_bin_sem_table[sem_id].id);
	}

	return OS_SUCCESS;
}/* end OS_BinSemCreate_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_BinSemDelete_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_BinSemDelete_Impl(uint32 sem_id)
{
	vSemaphoreDelete(OS_impl_bin_sem_table[sem_id].id);
	OS_impl_bin_sem_table[sem_id].id = (SemaphoreHandle_t)0xFFFF;

	return OS_SUCCESS;
}/* end OS_BinSemDelete_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_BinSemGive_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_BinSemGive_Impl(uint32 sem_id)
{
	xSemaphoreGive(OS_impl_bin_sem_table[sem_id].id);

	return OS_SUCCESS;
}/* end OS_BinSemGive_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_BinSemFlush_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_BinSemFlush_Impl(uint32 sem_id)
{
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_ERR_NOT_IMPLEMENTED;
}/* end OS_BinSemFlush_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_BinSemTake_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_BinSemTake_Impl(uint32 sem_id)
{
	if(xSemaphoreTake(OS_impl_bin_sem_table[sem_id].id, portMAX_DELAY) == pdTRUE)
	{
		return OS_SUCCESS;
	}
	else
	{
		return OS_SEM_FAILURE;
	}
}/* end OS_BinSemTake_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_BinSemTimedWait_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_BinSemTimedWait_Impl(uint32 sem_id, uint32 msecs)
{
	BaseType_t status;
	uint32 TimeInTicks;

	TimeInTicks = OS_Milli2Ticks(msecs);

	status = xSemaphoreTake(OS_impl_bin_sem_table[sem_id].id, TimeInTicks);

	switch(status)
	{
	case pdFALSE:
		return OS_SEM_TIMEOUT;
		break;

	case pdTRUE:
		return OS_SUCCESS;
		break;

	default:
		return OS_SEM_FAILURE;
		break;
	}
}/* end OS_BinSemTimedWait_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_BinSemGetInfo_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_BinSemGetInfo_Impl(uint32 sem_id, OS_bin_sem_prop_t *bin_prop)
{
	return OS_SUCCESS;
} /* end OS_BinSemGetInfo_Impl */

/****************************************************************************************
 COUNTING SEMAPHORE API
 ***************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_FreeRTOS_CountSemAPI_Impl_Init
 *
 *  Purpose: Local helper routine, not part of OSAL API.
 *
 *-----------------------------------------------------------------*/
int32 OS_FreeRTOS_CountSemAPI_Impl_Init(void)
{
    memset(OS_impl_count_sem_table, 0, sizeof(OS_impl_count_sem_table));

    return OS_SUCCESS;
} /* end OS_FreeRTOS_CountSemAPI_Impl_Init */

/*----------------------------------------------------------------
 *
 * Function: OS_CountSemCreate_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_CountSemCreate_Impl(uint32 sem_id, uint32 sem_initial_value, uint32 options)
{
	/*
	 ** Verify that the semaphore maximum value is not too high
	 */
	if(sem_initial_value > MAX_SEM_VALUE)
	{
		return OS_INVALID_SEM_VALUE;
	}

	OS_impl_count_sem_table[sem_id].id = xSemaphoreCreateCounting(MAX_SEM_VALUE, sem_initial_value);

	/* check if Create failed */
	if(OS_impl_count_sem_table[sem_id].id == NULL)
	{
		return OS_SEM_FAILURE;
	}

	return OS_SUCCESS;
}/* end OS_CountSemCreate_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_CountSemDelete_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_CountSemDelete_Impl(uint32 sem_id)
{
	vSemaphoreDelete(OS_impl_count_sem_table[sem_id].id);
	OS_impl_count_sem_table[sem_id].id = 0;

	return OS_SUCCESS;
}/* end OS_CountSemDelete_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_CountSemGive_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_CountSemGive_Impl(uint32 sem_id)
{
	if(xSemaphoreGive(OS_impl_count_sem_table[sem_id].id) != pdTRUE)
	{
		return OS_SEM_FAILURE;
	}
	else
	{
		return OS_SUCCESS;
	}
}/* end OS_CountSemGive_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_CountSemTake_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_CountSemTake_Impl(uint32 sem_id)
{
	if(xSemaphoreTake(OS_impl_count_sem_table[sem_id].id, portMAX_DELAY) != pdTRUE)
	{
		return OS_SEM_FAILURE;
	}

	return OS_SUCCESS;
}/* end OS_CountSemTake_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_CountSemTimedWait_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_CountSemTimedWait_Impl(uint32 sem_id, uint32 msecs)
{
	BaseType_t status;
	uint32 TimeInTicks;

	TimeInTicks = OS_Milli2Ticks(msecs);

	status = xSemaphoreTake(OS_impl_count_sem_table[sem_id].id, TimeInTicks);
	switch(status)
	{
	case pdFALSE:
		return OS_SEM_TIMEOUT;
		break;

	case pdTRUE:
		return OS_SUCCESS;
		break;

	default:
		return OS_SEM_FAILURE;
		break;
	}
}/* end OS_CountSemTimedWait_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_CountSemGetInfo_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_CountSemGetInfo_Impl(uint32 sem_id, OS_count_sem_prop_t *count_prop)
{
	return OS_SUCCESS;
} /* end OS_CountSemGetInfo_Impl */

/****************************************************************************************
 MUTEX API
 ****************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_FreeRTOS_MutexAPI_Impl_Init
 *
 *  Purpose: Local helper routine, not part of OSAL API.
 *
 *-----------------------------------------------------------------*/
int32 OS_FreeRTOS_MutexAPI_Impl_Init(void)
{
    memset(OS_impl_mut_sem_table, 0, sizeof(OS_impl_mut_sem_table));

    return OS_SUCCESS;
} /* end OS_FreeRTOS_MutexAPI_Impl_Init */

/*----------------------------------------------------------------
 *
 * Function: OS_MutSemCreate_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_MutSemCreate_Impl(uint32 sem_id, uint32 options)
{
	/*
	 ** Try to create the mutex
	 */
	OS_impl_mut_sem_table[sem_id].id = xSemaphoreCreateRecursiveMutex();
	if(OS_impl_mut_sem_table[sem_id].id == NULL)
	{
		return OS_SEM_FAILURE;
	}

	return OS_SUCCESS;
}/* end OS_MutSemCreate_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_MutSemDelete_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_MutSemDelete_Impl(uint32 sem_id)
{
	vSemaphoreDelete(OS_impl_mut_sem_table[sem_id].id);
	OS_impl_mut_sem_table[sem_id].id = (SemaphoreHandle_t)0xFFFF;

    return OS_SUCCESS;
}/* end OS_MutSemDelete_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_MutSemGive_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_MutSemGive_Impl(uint32 sem_id)
{
	/* Give the mutex */
	if(xSemaphoreGiveRecursive(OS_impl_mut_sem_table[sem_id].id) != pdTRUE)
	{
		return OS_SEM_FAILURE;
	}
	else
	{
		return OS_SUCCESS;
	}
}/* end OS_MutSemGive_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_MutSemTake_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_MutSemTake_Impl(uint32 sem_id)
{
	if(xSemaphoreTakeRecursive(OS_impl_mut_sem_table[sem_id].id, portMAX_DELAY) != pdTRUE)
	{
		return OS_SEM_FAILURE;
	}
	else
	{
		return OS_SUCCESS;
	}
}/* end OS_MutSemTake_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_MutSemGetInfo_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_MutSemGetInfo_Impl(uint32 sem_id, OS_mut_sem_prop_t *mut_prop)
{
	return OS_SUCCESS;
} /* end OS_MutSemGetInfo_Impl */

/****************************************************************************************
 INT API
 ****************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_IntAttachHandler_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_IntAttachHandler_Impl(uint32 InterruptNumber, osal_task_entry InterruptHandler, int32 parameter)
{
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_ERR_NOT_IMPLEMENTED;
}/* end OS_IntAttachHandler_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_IntUnlock_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_IntUnlock_Impl(int32 IntLevel)
{
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_SUCCESS;
}/* end OS_IntUnlock_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_IntLock_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_IntLock_Impl(void)
{
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_SUCCESS;
}/* end OS_IntLock_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_IntEnable_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_IntEnable_Impl(int32 Level)
{
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_SUCCESS;
}/* end OS_IntEnable_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_IntDisable_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_IntDisable_Impl(int32 Level)
{
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_SUCCESS;
}/* end OS_IntDisable_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_HeapGetInfo_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_HeapGetInfo_Impl(OS_heap_prop_t *heap_prop)
{
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_ERR_NOT_IMPLEMENTED;
}/* end OS_HeapGetInfo_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_IntSetMask_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_IntSetMask_Impl(uint32 MaskSetting)
{
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_ERR_NOT_IMPLEMENTED;
} /* end OS_IntSetMask_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_IntGetMask_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_IntGetMask_Impl(uint32 * MaskSettingPtr)
{
    *MaskSettingPtr = 0;
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_ERR_NOT_IMPLEMENTED;
} /* end OS_IntGetMask_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_FPUExcAttachHandler_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_FPUExcAttachHandler_Impl(uint32 ExceptionNumber, void * ExceptionHandler, int32 parameter)
{
    /*
    ** Not implemented in FreeRTOS.
    */
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_ERR_NOT_IMPLEMENTED;
} /* end OS_FPUExcAttachHandler_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_FPUExcEnable_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_FPUExcEnable_Impl(int32 ExceptionNumber)
{
    /*
    ** Not implemented in FreeRTOS.
    */
    return OS_SUCCESS;
} /* end OS_FPUExcEnable_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_FPUExcDisable_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_FPUExcDisable_Impl(int32 ExceptionNumber)
{
    /*
    ** Not implemented in FreeRTOS.
    */
    return OS_SUCCESS;
} /* end OS_FPUExcDisable_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_FPUExcSetMask_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_FPUExcSetMask_Impl(uint32 mask)
{
    /*
    ** Not implemented in FreeRTOS.
    */
    return OS_SUCCESS;
} /* end OS_FPUExcSetMask_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_FPUExcGetMask_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_FPUExcGetMask_Impl(uint32 *mask)
{
    /*
    ** Not implemented in FreeRTOS.
    */
    return OS_SUCCESS;
} /* end OS_FPUExcGetMask_Impl */

/****************************************************************************************
 CONSOLE OUTPUT
 ****************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_ConsoleOutput_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
void  OS_ConsoleOutput_Impl(uint32 local_id)
{
    uint32 StartPos;
    uint32 EndPos;
    long WriteSize;
    OS_console_internal_record_t *console;
    int fd;

    console = &OS_console_table[local_id];
    fd = OS_impl_console_table[local_id].out_fd;
    StartPos = console->ReadPos;
    EndPos = console->WritePos;
    while(StartPos != EndPos)
    {
        if(StartPos > EndPos)
        {
            /* handle wrap */
            WriteSize = console->BufSize - StartPos;
        }
        else
        {
            WriteSize = EndPos - StartPos;
        }

        WriteSize = write(fd, &console->BufBase[StartPos], WriteSize);

        if(WriteSize <= 0)
        {
            /* write error */
            /* This debug message _might_ go to the same console,
             * but might not, so its worth a shot. */
            OS_DEBUG("%s(): write(): %s\n", __func__, strerror(errno));
            break;
        }

        StartPos += WriteSize;
        if(StartPos >= console->BufSize)
        {
            /* handle wrap */
            StartPos = 0;
        }
    }

    /* Update the global with the new read location */
    console->ReadPos = StartPos;
} /* end OS_ConsoleOutput_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_ConsoleWakeup_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
void  OS_ConsoleWakeup_Impl(uint32 local_id)
{
	OS_impl_console_internal_record_t *local = &OS_impl_console_table[local_id];

	if(local->is_async)
	{
		/* post the sem for the utility task to run */
		xSemaphoreGive(local->data_sem);
	}
	else
	{
		/* output directly */
		OS_ConsoleOutput_Impl(local_id);
	}
}/* end OS_ConsoleWakeup_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_ConsoleTask_Entry
 *
 *  Purpose: Local helper routine, not part of OSAL API.
 *
 *-----------------------------------------------------------------*/
static void OS_ConsoleTask_Entry(int arg)
{
    uint32 local_id = arg;
	OS_impl_console_internal_record_t *local;

	local = &OS_impl_console_table[local_id];
	while(true)
	{
		OS_ConsoleOutput_Impl(local_id);
		xSemaphoreTake(local->data_sem, portMAX_DELAY);
	}
} /* end OS_ConsoleTask_Entry */

/*----------------------------------------------------------------
 *
 * Function: OS_ConsoleCreate_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_ConsoleCreate_Impl(uint32 local_id)
{
	OS_impl_console_internal_record_t *local = &OS_impl_console_table[local_id];
	TaskHandle_t consoletask;
	int32 return_code = -1;

	if(local_id == 0)
	{
		return_code = OS_SUCCESS;
		local->is_async = OS_CONSOLE_ASYNC;
		local->out_fd = OSAL_CONSOLE_FILENO;

		if(local->is_async)
		{
			local->data_sem = xSemaphoreCreateCounting(MAX_SEM_VALUE, 0);

			if(local->data_sem == NULL)
			{
				return_code = OS_SEM_FAILURE;
			}
			else
			{
				BaseType_t status;
				status = xTaskCreate((TaskFunction_t) OS_ConsoleTask_Entry,
						NULL,
						OS_CONSOLE_TASK_STACKSIZE,
						(void *)local_id,
						OS_CONSOLE_TASK_PRIORITY,
						&consoletask);

				if(status != pdPASS)
				{
					vSemaphoreDelete(local->data_sem);
					local->data_sem = 0;
					return_code = OS_ERROR;
				}
			}
		}
	}
	else
	{
		/* only one physical console device is implemented */
		return_code = OS_ERR_NOT_IMPLEMENTED;
	}

	return return_code;
}/* end OS_ConsoleCreate_Impl */
