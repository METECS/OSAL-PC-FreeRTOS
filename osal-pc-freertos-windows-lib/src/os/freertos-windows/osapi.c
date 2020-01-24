/*
 * This file contains some of the OS APIs abstraction layer. It
 * contains those APIs that call the  OS. In this case the OS is FreeRTOS.
 *
 * Based on src/os/rtems/osapi.c from the OSAL distribution wit the
 * following license:
 *
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
#include <FreeRTOSEx.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"
#include "common_types.h"
#include "osapi.h"

#ifndef configFREERTOS_RUN_AS_SIM
#error configFREERTOS_RUN_AS_SIM must be set to 0 or 1 in FreeRTOSConfig.h
#endif
#if configFREERTOS_RUN_AS_SIM == 1
#include "winbase.h"
#endif

#include "osapi-os-filesys-ex.h"
#include "osapi-os-loader-ex.h"
#include "osapi-os-timer-ex.h"

/*
 ** Function Prototypes
 */
uint32 OS_FindCreator(void);

/*
 ** Stub task to call another task and then clean itself up
 */
void vStubTask(void *pvParameters)
{
	/* The parameter is the function to call */
	void (*funcPtr)(void) = pvParameters;
	funcPtr();
	OS_TaskExit();
}

/****************************************************************************************
 DEFINES
 ****************************************************************************************/
#define OSAPI_MAX_PRIORITY                (configMAX_PRIORITIES)
#define MAX_SEM_VALUE               0x7FFFFFFF
#define UNINITIALIZED               0

#define OS_SHUTDOWN_MAGIC_NUMBER    0xABADC0DE

/****************************************************************************************
 GLOBAL DATA
 ****************************************************************************************/
/*  tables for the properties of objects */

/*tasks */
typedef struct {
	int free;
	int deleted;
	TaskHandle_t id;
	char name[OS_MAX_API_NAME];
	char original_name[MAX_API_NAME_INCOMING];
	int creator;
	uint32 stack_size;
	uint32 priority;
	osal_task_entry delete_hook_pointer;
	uint32 entry_number; /* This is the entry number of each instance of this structure in the task table */

} OS_task_internal_record_t;

/* queues */
typedef struct {
	int free;
	int deleted;
	QueueHandle_t id;
	uint32 max_size;
	char name[OS_MAX_API_NAME];
	char original_name[MAX_API_NAME_INCOMING];
	int creator;
} OS_queue_internal_record_t;

/* Binary Semaphores */
typedef struct {
	int free;
	int deleted;
	SemaphoreHandle_t id;
	char name[OS_MAX_API_NAME];
	char original_name[MAX_API_NAME_INCOMING];
	int creator;
} OS_bin_sem_internal_record_t;

/* Counting Semaphores */
typedef struct {
	int free;
	SemaphoreHandle_t id;
	char name[OS_MAX_API_NAME];
	char original_name[MAX_API_NAME_INCOMING];
	int creator;
} OS_count_sem_internal_record_t;

/* Mutexes */
typedef struct {
	int free;
	int deleted;
	SemaphoreHandle_t id;
	char name[OS_MAX_API_NAME];
	char original_name[MAX_API_NAME_INCOMING];
	int creator;
} OS_mut_sem_internal_record_t;

/* function pointer type */
typedef void (*FuncPtr_t)(void);

/* Tables where the OS object information is stored */
#define OS_TASK_TABLE_SIZE (OS_MAX_TASKS*2)
OS_task_internal_record_t OS_task_table[OS_TASK_TABLE_SIZE];
static uint32 task_count = 0;

#define OS_QUEUE_TABLE_SIZE (OS_MAX_QUEUES * 2)
OS_queue_internal_record_t OS_queue_table[OS_QUEUE_TABLE_SIZE];
static uint32 queue_count = 0;

#define OS_BIN_SEM_TABLE_SIZE (OS_MAX_BIN_SEMAPHORES*2)
OS_bin_sem_internal_record_t OS_bin_sem_table[OS_BIN_SEM_TABLE_SIZE];
static uint32 bin_sem_count = 0;

OS_count_sem_internal_record_t OS_count_sem_table[OS_MAX_COUNT_SEMAPHORES];

#define OS_MUTEX_TABLE_SIZE (OS_MAX_MUTEXES*2)
OS_mut_sem_internal_record_t OS_mut_sem_table[OS_MUTEX_TABLE_SIZE];
static uint32 mut_sem_count = 0;

SemaphoreHandle_t OS_task_table_sem;
SemaphoreHandle_t OS_queue_table_sem;
SemaphoreHandle_t OS_bin_sem_table_sem;
SemaphoreHandle_t OS_mut_sem_table_sem;
SemaphoreHandle_t OS_count_sem_table_sem;

uint32 OS_printf_enabled = TRUE;
volatile uint32 OS_shutdown = FALSE;

static int32 adjust_seconds = 0;
static int32 adjust_microseconds = 0;

/* A counter to ensure generated names are unique */
static uint8 name_counter = 0;

/* A named pipe used to control the progress of the FreeRTOS application */
#ifndef configFREERTOS_RUN_AS_SIM
#error configFREERTOS_RUN_AS_SIM must be set to 0 or 1 in FreeRTOSConfig.h
#endif
#if configFREERTOS_RUN_AS_SIM == 1
HANDLE freertos_sync_pipe = INVALID_HANDLE_VALUE;
#endif

/****************************************************************************************
 INITIALIZATION FUNCTION
 ****************************************************************************************/

/*---------------------------------------------------------------------------------------
 Name: OS_API_Init

 Purpose: Initialize the tables that the OS API uses to keep track of information
 about objects

 returns: OS_SUCCESS or OS_ERROR
 ---------------------------------------------------------------------------------------*/
int32 OS_API_Init(void) {
	int i;
	int32 return_code = OS_SUCCESS;

	/* Initialize Task Table */
	for (i = 0; i < OS_TASK_TABLE_SIZE; i++) {
		OS_task_table[i].free = TRUE;
		OS_task_table[i].deleted = FALSE;
		OS_task_table[i].id = UNINITIALIZED;
		OS_task_table[i].creator = UNINITIALIZED;
		OS_task_table[i].delete_hook_pointer = NULL;
		OS_task_table[i].name[0] = '\0';
		OS_task_table[i].original_name[0] = '\0';
		OS_task_table[i].entry_number = i;
	}

	/* Initialize Message Queue Table */
	for (i = 0; i < OS_QUEUE_TABLE_SIZE; i++) {
		OS_queue_table[i].free = TRUE;
		OS_queue_table[i].deleted = FALSE;
		OS_queue_table[i].id = UNINITIALIZED;
		OS_queue_table[i].creator = UNINITIALIZED;
		OS_queue_table[i].name[0] = '\0';
		OS_queue_table[i].original_name[0] = '\0';
	}

	/* Initialize Binary Semaphore Table */
	for (i = 0; i < OS_BIN_SEM_TABLE_SIZE; i++) {
		OS_bin_sem_table[i].free = TRUE;
		OS_bin_sem_table[i].deleted = FALSE;
		OS_bin_sem_table[i].id = UNINITIALIZED;
		OS_bin_sem_table[i].creator = UNINITIALIZED;
		OS_bin_sem_table[i].name[0] = '\0';
		OS_bin_sem_table[i].original_name[0] = '\0';
	}

	/* Initialize Counting Semaphore Table */
	for (i = 0; i < OS_MAX_COUNT_SEMAPHORES; i++) {
		OS_count_sem_table[i].free = TRUE;
		OS_count_sem_table[i].id = UNINITIALIZED;
		OS_count_sem_table[i].creator = UNINITIALIZED;
		OS_count_sem_table[i].name[0] = '\0';
		OS_count_sem_table[i].original_name[0] = '\0';
	}

	/* Initialize Mutex Semaphore Table */
	for (i = 0; i < OS_MUTEX_TABLE_SIZE; i++) {
		OS_mut_sem_table[i].free = TRUE;
		OS_mut_sem_table[i].deleted = FALSE;
		OS_mut_sem_table[i].id = UNINITIALIZED;
		OS_mut_sem_table[i].creator = UNINITIALIZED;
		OS_mut_sem_table[i].name[0] = '\0';
		OS_mut_sem_table[i].original_name[0] = '\0';
	}

	/*
	 ** Initialize the module loader
	 */
#ifdef OS_INCLUDE_MODULE_LOADER
	return_code = OS_ModuleTableInit();
	if (return_code != OS_SUCCESS) {
		return (return_code);
	}
#endif

	/*
	 ** Initialize the Timer API
	 */
	return_code = OS_TimerAPIInit();
	if (return_code == OS_ERROR) {
		return (return_code);
	}

	/*
	 ** Initialize the internal table Mutexes
	 */
	OS_task_table_sem = xSemaphoreCreateMutex();
	if (OS_task_table_sem == NULL) {
		return_code = OS_ERROR;
		return (return_code);
	}

	OS_queue_table_sem = xSemaphoreCreateMutex();
	if (OS_queue_table_sem == NULL) {
		return_code = OS_ERROR;
		return (return_code);
	}

	OS_bin_sem_table_sem = xSemaphoreCreateMutex();
	if (OS_bin_sem_table_sem == NULL) {
		return_code = OS_ERROR;
		return (return_code);
	}

	OS_count_sem_table_sem = xSemaphoreCreateMutex();
	if (OS_count_sem_table_sem == NULL) {
		return_code = OS_ERROR;
		return (return_code);
	}

	OS_mut_sem_table_sem = xSemaphoreCreateMutex();
	if (OS_mut_sem_table_sem == NULL) {
		return_code = OS_ERROR;
		return (return_code);
	}

#ifndef configFREERTOS_RUN_AS_SIM
#error configFREERTOS_RUN_AS_SIM must be set to 0 or 1 in FreeRTOSConfig.h
#endif
#if configFREERTOS_RUN_AS_SIM == 1
	freertos_sync_pipe = CreateNamedPipeA(configFREERTOS_SYNC_PIPE_NAME,
			PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
			PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
			1, 16, 16, 0, NULL);
	if (freertos_sync_pipe == INVALID_HANDLE_VALUE) {
		return_code = OS_ERROR;
		return (return_code);
	}
#endif

	/*
	 ** File system init
	 */
	return_code = OS_FS_Init();

	return (return_code);

} /* end OS_API_Init */

/*---------------------------------------------------------------------------------------
 Name: OS_ApplicationExit

 Purpose: Indicates that the OSAL application should exit and return control to the OS
 This is intended for e.g. scripted unit testing where the test needs to end
 without user intervention.  This function does not return.

 NOTES: This exits the entire process including tasks that have been created.
 It does not return.  Production embedded code typically would not ever call this.

 ---------------------------------------------------------------------------------------*/
void OS_ApplicationExit(int32 Status) {
	if (Status == OS_SUCCESS) {
		exit(EXIT_SUCCESS);
	} else {
		exit(EXIT_FAILURE);
	}
}

/*---------------------------------------------------------------------------------------
 Name: OS_DeleteAllObjects

 Purpose: This task will delete all objects allocated by this instance of OSAL
 May be used during shutdown or by the unit tests to purge all state

 returns: no value
 ---------------------------------------------------------------------------------------*/
void OS_DeleteAllObjects(void) {
	uint32 i;

	for (i = 0; i < OS_TASK_TABLE_SIZE; ++i) {
		OS_TaskDelete(i);
	}
	for (i = 0; i < OS_QUEUE_TABLE_SIZE; ++i) {
		OS_QueueDelete(i);
	}
	for (i = 0; i < OS_MUTEX_TABLE_SIZE; ++i) {
		OS_MutSemDelete(i);
	}
	for (i = 0; i < OS_MAX_COUNT_SEMAPHORES; ++i) {
		OS_CountSemDelete(i);
	}
	for (i = 0; i < OS_BIN_SEM_TABLE_SIZE; ++i) {
		OS_BinSemDelete(i);
	}
	for (i = 0; i < OS_MAX_TIMERS; ++i) {
		OS_TimerDelete(i);
	}
	for (i = 0; i < OS_MAX_MODULES; ++i) {
		OS_ModuleUnload(i);
	}
	for (i = 0; i < OS_MAX_NUM_OPEN_FILES; ++i) {
		OS_close(i);
	}

#ifndef configFREERTOS_RUN_AS_SIM
#error configFREERTOS_RUN_AS_SIM must be set to 0 or 1 in FreeRTOSConfig.h
#endif
#if configFREERTOS_RUN_AS_SIM == 1
	if (freertos_sync_pipe != INVALID_HANDLE_VALUE) {
		CloseHandle(freertos_sync_pipe);
		freertos_sync_pipe = INVALID_HANDLE_VALUE;
	}
#endif
}

/*---------------------------------------------------------------------------------------
 Name: OS_IdleLoop

 Purpose: Should be called after all initialization is done
 This thread may be used to wait for and handle external events
 Typically just waits forever until "OS_shutdown" flag becomes true.

 returns: no value
 ---------------------------------------------------------------------------------------*/
void OS_IdleLoop() {
	while (OS_shutdown != OS_SHUTDOWN_MAGIC_NUMBER) {
		vTaskDelay(100);
	}
}

/*---------------------------------------------------------------------------------------
 Name: OS_ApplicationShutdown

 Purpose: Indicates that the OSAL application should perform an orderly shutdown
 of ALL tasks, clean up all resources, and exit the application.

 returns: none

 ---------------------------------------------------------------------------------------*/
void OS_ApplicationShutdown(uint8 flag) {
	if (flag == TRUE) {
		OS_shutdown = OS_SHUTDOWN_MAGIC_NUMBER;
	}
}

/****************************************************************************************
 TASK API
 ****************************************************************************************/

/*---------------------------------------------------------------------------------------
 Name: OS_TaskCreate

 Purpose: Creates a task and starts running it.

 returns: OS_INVALID_POINTER if any of the necessary pointers are NULL
 OS_ERR_NAME_TOO_LONG if the name of the task is too long to be copied
 OS_ERR_INVALID_PRIORITY if the priority is bad
 OS_ERR_NO_FREE_IDS if there can be no more tasks created
 OS_ERR_NAME_TAKEN if the name specified is already used by a task
 OS_ERROR if the operating system calls fail
 OS_SUCCESS if success

 NOTES: task_id is passed back to the user as the ID. stack_pointer is usually null.


 ---------------------------------------------------------------------------------------*/

int32 OS_TaskCreate(uint32 *task_id, const char *task_name,
		osal_task_entry function_pointer, uint32 *stack_pointer,
		uint32 stack_size, uint32 priority, uint32 flags) {
	uint32 possible_taskid;
	uint32 i;
	BaseType_t status;

	/* Check for NULL pointers */
	if ((task_name == NULL) || (function_pointer == NULL)
			|| (task_id == NULL)) {
		return OS_INVALID_POINTER;
	}

	char os_unique_name[OS_MAX_API_NAME];
	snprintf(os_unique_name, OS_MAX_API_NAME, "%-*.*s%02x", (OS_MAX_API_NAME-3), (OS_MAX_API_NAME-3), task_name, name_counter++);

	/* we don't want to allow names too long*/
	/* if truncated, two names might be the same */
	if (strlen(task_name) >= MAX_API_NAME_INCOMING) {
		return OS_ERR_NAME_TOO_LONG;
	}
	if (strlen(os_unique_name) >= OS_MAX_API_NAME) {
		return OS_ERR_NAME_TOO_LONG;
	}

	/* Because all of cFS and OSAL have been written with the assumption that
	 * priorities range from 0 (highest priority) to 255 (lowest priority)
	 * Let's normalize that range into FreeRTOS priorities
	 */
	priority = (255 - priority) / (256/configMAX_PRIORITIES);

	/* Check for bad priority */
	if (priority >= OSAPI_MAX_PRIORITY) {
		return OS_ERR_INVALID_PRIORITY;
	}

	/* Check Parameters */
	status = xSemaphoreTake(OS_task_table_sem, portMAX_DELAY);

	/* Ensure a free resource is available */
	if (task_count >= OS_MAX_TASKS) {
		status = xSemaphoreGive(OS_task_table_sem);
		return OS_ERR_NO_FREE_IDS;
	}

	for (possible_taskid = 0; possible_taskid < OS_TASK_TABLE_SIZE;
			possible_taskid++) {
		if (OS_task_table[possible_taskid].free == TRUE) {
			break;
		}
	}

	/* Check to see if the id is out of bounds */
	if (possible_taskid >= OS_TASK_TABLE_SIZE
			|| OS_task_table[possible_taskid].free != TRUE) {
		status = xSemaphoreGive(OS_task_table_sem);
		return OS_ERR_NO_FREE_IDS;
	}

	/* Check to see if the name is already taken */
	for (i = 0; i < OS_TASK_TABLE_SIZE; i++) {
		if ((OS_task_table[i].free == FALSE && OS_task_table[i].deleted == FALSE)
				&& (strcmp((char*) task_name, OS_task_table[i].original_name) == 0)) {
			status = xSemaphoreGive(OS_task_table_sem);
			return OS_ERR_NAME_TAKEN;
		}
	}
	/* Set the possible task Id to not free so that
	 * no other task can try to use it */

	OS_task_table[possible_taskid].free = FALSE;
	status = xSemaphoreGive(OS_task_table_sem);

	status = xTaskCreate((TaskFunction_t) vStubTask, os_unique_name,
			stack_size, function_pointer, priority, &OS_task_table[possible_taskid].id);

	if (status != pdPASS) {
		status = xSemaphoreTake(OS_task_table_sem, portMAX_DELAY);
		OS_task_table[possible_taskid].free = TRUE;
		status = xSemaphoreGive(OS_task_table_sem);
		return OS_ERROR;
	}

	/* Set the task_id to the id that was found available
	 Set the name of the task, the stack size, and priority */
	*task_id = possible_taskid;

	/* this Id no longer free */
	status = xSemaphoreTake(OS_task_table_sem, portMAX_DELAY);
	strcpy(OS_task_table[*task_id].original_name, (char*) task_name);
	strcpy(OS_task_table[*task_id].name, (char*) os_unique_name);
	OS_task_table[*task_id].creator = OS_FindCreator();
	OS_task_table[*task_id].stack_size = stack_size;
	OS_task_table[*task_id].priority = priority;
	task_count++;
	status = xSemaphoreGive(OS_task_table_sem);

	return OS_SUCCESS;
} /* end OS_TaskCreate */

/*--------------------------------------------------------------------------------------
 Name: OS_TaskDelete

 Purpose: Deletes the specified Task and removes it from the OS_task_table.

 returns: OS_ERR_INVALID_ID if the ID given to it is invalid
 OS_ERROR if the OS delete call fails
 OS_SUCCESS if success
 ---------------------------------------------------------------------------------------*/

int32 OS_TaskDelete(uint32 task_id) {
	FuncPtr_t FunctionPointer;

	/*
	 ** Check to see if the task_id given is valid
	 */
	if (task_id >= OS_TASK_TABLE_SIZE || OS_task_table[task_id].free == TRUE || OS_task_table[task_id].deleted == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	/*
	 ** Call the task Delete hook if there is one.
	 */
	if (OS_task_table[task_id].delete_hook_pointer != NULL) {
		FunctionPointer =
				(FuncPtr_t) OS_task_table[task_id].delete_hook_pointer;
		(*FunctionPointer)();
	}

	/* Try to delete the task */
	vTaskDelete(OS_task_table[task_id].id);

	/*
	 * Now that the task is deleted, remove its
	 * "presence" in OS_task_table
	 */
	xSemaphoreTake(OS_task_table_sem, portMAX_DELAY);
	OS_task_table[task_id].free = FALSE;
	OS_task_table[task_id].deleted = TRUE;
	OS_task_table[task_id].id = (TaskHandle_t)0xFFFF;
	OS_task_table[task_id].name[0] = '\0';
	OS_task_table[task_id].creator = UNINITIALIZED;
	OS_task_table[task_id].stack_size = UNINITIALIZED;
	OS_task_table[task_id].priority = UNINITIALIZED;
	OS_task_table[task_id].delete_hook_pointer = NULL;
	task_count--;
	xSemaphoreGive(OS_task_table_sem);

	return OS_SUCCESS;
}/* end OS_TaskDelete */

/*--------------------------------------------------------------------------------------
 Name:    OS_TaskExit

 Purpose: Exits the calling task and removes it from the OS_task_table.

 returns: Nothing
 ---------------------------------------------------------------------------------------*/
void OS_TaskExit() {
	uint32 task_id;

	task_id = OS_TaskGetId();

	xSemaphoreTake(OS_task_table_sem, portMAX_DELAY);

	OS_task_table[task_id].free = TRUE;
	OS_task_table[task_id].id = UNINITIALIZED;
	OS_task_table[task_id].name[0] = '\0';
	OS_task_table[task_id].creator = UNINITIALIZED;
	OS_task_table[task_id].stack_size = UNINITIALIZED;
	OS_task_table[task_id].priority = UNINITIALIZED;
	OS_task_table[task_id].delete_hook_pointer = NULL;
	task_count--;
	xSemaphoreGive(OS_task_table_sem);

	vTaskDelete(xTaskGetCurrentTaskHandle());
}/*end OS_TaskExit */

/*---------------------------------------------------------------------------------------
 Name: OS_TaskDelay

 Purpose: Delay a task for specified amount of milliseconds

 returns: OS_ERROR if sleep fails
 OS_SUCCESS if success
 ---------------------------------------------------------------------------------------*/
int32 OS_TaskDelay(uint32 milli_second) {
	TickType_t ticks;

	ticks = OS_Milli2Ticks(milli_second);
	vTaskDelay(ticks);

	return (OS_SUCCESS);
}/* end OS_TaskDelay */
/*---------------------------------------------------------------------------------------
 Name: OS_TaskSetPriority

 Purpose: Sets the given task to a new priority

 returns: OS_ERR_INVALID_ID if the ID passed to it is invalid
 OS_ERR_INVALID_PRIORITY if the priority is greater than the max
 allowed
 OS_ERROR if the OS call to change the priority fails
 OS_SUCCESS if success
 ---------------------------------------------------------------------------------------*/
int32 OS_TaskSetPriority(uint32 task_id, uint32 new_priority) {

	/* Check Parameters */
	if (task_id >= OS_TASK_TABLE_SIZE || OS_task_table[task_id].free == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	if (new_priority > MAX_PRIORITY) {
		return OS_ERR_INVALID_PRIORITY;
	}

	/* Set Task Priority */
	vTaskPrioritySet(OS_task_table[task_id].id, new_priority);
	OS_task_table[task_id].priority = new_priority;

	return OS_SUCCESS;

}/* end OS_TaskSetPriority */

/*---------------------------------------------------------------------------------------
 Name: OS_TaskRegister

 Purpose: Registers the calling task id with the task by adding the var to the tcb
 It searches the OS_task_table to find the task_id corresponding to the
 tcb_id

 Returns: OS_ERR_INVALID_ID if there the specified ID could not be found
 OS_ERROR if the OS call fails
 OS_SUCCESS if success
 ---------------------------------------------------------------------------------------*/

int32 OS_TaskRegister(void) {
	TaskHandle_t os_task_id;
	int i;
	uint32 task_id;

	/*
	 ** Get Task Id
	 */
	os_task_id = xTaskGetCurrentTaskHandle();
	if (os_task_id == NULL) {
		return (OS_ERROR);
	}

	for (i = 0; i < OS_TASK_TABLE_SIZE; i++) {
		if (OS_task_table[i].id == os_task_id)
			break;
	}

	task_id = i;

	if (task_id >= OS_TASK_TABLE_SIZE) {
		return OS_ERR_INVALID_ID;
	}

	vTaskSetThreadLocalStoragePointer(os_task_id, 0,
			&OS_task_table[task_id].entry_number);

	return OS_SUCCESS;

}/* end OS_TaskRegister */

/*---------------------------------------------------------------------------------------
 Name: OS_TaskGetId

 Purpose: This function returns the #defined task id of the calling task

 Notes: The OS_task_key is initialized by the task switch if AND ONLY IF the
 OS_task_key has been registered via OS_TaskRegister(..).  If this is not
 called prior to this call, the value will be old and wrong.
 ---------------------------------------------------------------------------------------*/
uint32 OS_TaskGetId(void) {
	TaskHandle_t current_task_handle = xTaskGetCurrentTaskHandle();

	for (int i = 0; i < OS_TASK_TABLE_SIZE; i++) {
		if (OS_task_table[i].free != TRUE
				&& OS_task_table[i].id == current_task_handle) {
			return i;
		}
	}

	/*
	 ** The name was not found in the table,
	 **  or it was, and the sem_id isn't valid anymore
	 */
	return OS_ERR_NAME_NOT_FOUND;;
}/* end OS_TaskGetId */

/*--------------------------------------------------------------------------------------
 Name: OS_TaskGetIdByName

 Purpose: This function tries to find a task Id given the name of a task

 Returns: OS_INVALID_POINTER if the pointers passed in are NULL
 OS_ERR_NAME_TOO_LONG if th ename to found is too long to begin with
 OS_ERR_NAME_NOT_FOUND if the name wasn't found in the table
 OS_SUCCESS if SUCCESS
 ---------------------------------------------------------------------------------------*/
int32 OS_TaskGetIdByName(uint32 *task_id, const char *task_name) {
	uint32 i;

	if (task_id == NULL || task_name == NULL) {
		return OS_INVALID_POINTER;
	}

	/* we don't want to allow names too long because they won't be found at all */
	if (strlen(task_name) >= MAX_API_NAME_INCOMING) {
		return OS_ERR_NAME_TOO_LONG;
	}

	for (i = 0; i < OS_TASK_TABLE_SIZE; i++) {
		if (OS_task_table[i].free != TRUE && OS_task_table[i].deleted == FALSE
				&& (strcmp(OS_task_table[i].original_name, (char*) task_name) == 0)) {
			*task_id = i;
			return OS_SUCCESS;
		}
	}

	/* The name was not found in the table,
	 *  or it was, and the task_id isn't valid anymore */
	return OS_ERR_NAME_NOT_FOUND;
}/* end OS_TaskGetIdByName */

/*---------------------------------------------------------------------------------------
 Name: OS_TaskGetInfo

 Purpose: This function will pass back a pointer to structure that contains
 all of the relevant info (creator, stack size, priority, name) about the
 specified task.

 Returns: OS_ERR_INVALID_ID if the ID passed to it is invalid
 OS_INVALID_POINTER if the task_prop pointer is NULL
 OS_SUCCESS if it copied all of the relevant info over

 ---------------------------------------------------------------------------------------*/
int32 OS_TaskGetInfo(uint32 task_id, OS_task_prop_t *task_prop) {
	/* Check to see that the id given is valid */
	if (task_id >= OS_TASK_TABLE_SIZE || OS_task_table[task_id].free == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	if (task_prop == NULL) {
		return OS_INVALID_POINTER;
	}

	/* put the info into the stucture */
	xSemaphoreTake(OS_task_table_sem, portMAX_DELAY);
	task_prop->creator = OS_task_table[task_id].creator;
	task_prop->stack_size = OS_task_table[task_id].stack_size;
	task_prop->priority = OS_task_table[task_id].priority;
	//	task_prop->OStask_id = (uint32) OS_task_table[task_id].id;
	task_prop->OStask_id = (uintptr_t) OS_task_table[task_id].id;
	xSemaphoreGive(OS_task_table_sem);

	strcpy(task_prop->name, OS_task_table[task_id].original_name);

	return OS_SUCCESS;
} /* end OS_TaskGetInfo */

/*--------------------------------------------------------------------------------------
 Name: OS_TaskInstallDeleteHandler

 Purpose: Installs a handler for when the task is deleted.

 returns: status
 ---------------------------------------------------------------------------------------*/
int32 OS_TaskInstallDeleteHandler(osal_task_entry function_pointer) {
	uint32 task_id;

	task_id = OS_TaskGetId();

	if (task_id >= OS_TASK_TABLE_SIZE) {
		return (OS_ERR_INVALID_ID);
	}

	xSemaphoreTake(OS_task_table_sem, portMAX_DELAY);

	if (OS_task_table[task_id].free != FALSE) {
		/*
		 ** Somehow the calling task is not registered
		 */
		xSemaphoreGive(OS_task_table_sem);
		return (OS_ERR_INVALID_ID);
	}

	/*
	 ** Install the pointer
	 */
	OS_task_table[task_id].delete_hook_pointer = function_pointer;

	xSemaphoreGive(OS_task_table_sem);

	return (OS_SUCCESS);

}/*end OS_TaskInstallDeleteHandler */

/****************************************************************************************
 MESSAGE QUEUE API
 ****************************************************************************************/
/*---------------------------------------------------------------------------------------
 Name: OS_QueueCreate

 Purpose: Create a message queue which can be refered to by name or ID

 Returns: OS_INVALID_POINTER if a pointer passed in is NULL
 OS_ERR_NAME_TOO_LONG if the name passed in is too long
 OS_ERR_NO_FREE_IDS if there are already the max queues created
 OS_ERR_NAME_TAKEN if the name is already being used on another queue
 OS_ERROR if the OS create call fails
 OS_SUCCESS if success

 Notes: the flahs parameter is unused.
 ---------------------------------------------------------------------------------------*/

int32 OS_QueueCreate(uint32 *queue_id, const char *queue_name,
		uint32 queue_depth, uint32 data_size, uint32 flags) {
	uint32 possible_qid;
	uint32 i;

	/* Check Parameters */
	if (queue_id == NULL || queue_name == NULL) {
		return OS_INVALID_POINTER;
	}

	char os_unique_name[OS_MAX_API_NAME];
	snprintf(os_unique_name, OS_MAX_API_NAME, "%-*.*s%02x", (OS_MAX_API_NAME-3), (OS_MAX_API_NAME-3), queue_name, name_counter++);

	/* we don't want to allow names too long*/
	/* if truncated, two names might be the same */
	if (strlen(queue_name) >= MAX_API_NAME_INCOMING) {
		return OS_ERR_NAME_TOO_LONG;
	}
	if (strlen(os_unique_name) >= OS_MAX_API_NAME) {
		return OS_ERR_NAME_TOO_LONG;
	}

	if (queue_depth > OS_QUEUE_MAX_DEPTH) {
		return OS_QUEUE_INVALID_SIZE;
	}

	xSemaphoreTake(OS_queue_table_sem, portMAX_DELAY);

	/* Ensure a free resource is available */
	if (queue_count >= OS_MAX_QUEUES) {
		xSemaphoreGive(OS_queue_table_sem);
		return OS_ERR_NO_FREE_IDS;
	}

	for (possible_qid = 0; possible_qid < OS_QUEUE_TABLE_SIZE; possible_qid++) {
		if (OS_queue_table[possible_qid].free == TRUE)
			break;
	}

	if (possible_qid >= OS_QUEUE_TABLE_SIZE
			|| OS_queue_table[possible_qid].free != TRUE) {
		xSemaphoreGive(OS_queue_table_sem);
		return OS_ERR_NO_FREE_IDS;
	}

	/* Check to see if the name is already taken */
	for (i = 0; i < OS_QUEUE_TABLE_SIZE; i++) {
		if ((OS_queue_table[i].free == FALSE && OS_queue_table[i].deleted == FALSE)
				&& strcmp((char*) queue_name, OS_queue_table[i].original_name) == 0) {
			xSemaphoreGive(OS_queue_table_sem);
			return OS_ERR_NAME_TAKEN;
		}
	}

	/* set the ID free to false to prevent other tasks from grabbing it */
	OS_queue_table[possible_qid].free = FALSE;
	xSemaphoreGive(OS_queue_table_sem);

	/*
	 ** Create the message queue.
	 */
	OS_queue_table[possible_qid].id = xQueueCreate(queue_depth, data_size);

	/*
	 ** If the operation failed, report the error
	 */
	if (OS_queue_table[possible_qid].id == NULL) {
		xSemaphoreTake(OS_queue_table_sem, portMAX_DELAY);
		OS_queue_table[possible_qid].free = TRUE;
		OS_queue_table[possible_qid].id = 0;
		xSemaphoreGive(OS_queue_table_sem);
		return OS_ERROR;
	}

	/* Set the queue_id to the id that was found available*/
	/* Set the name of the queue, and the creator as well */
	*queue_id = possible_qid;

	xSemaphoreTake(OS_queue_table_sem, portMAX_DELAY);

	OS_queue_table[*queue_id].max_size = data_size;
	strcpy(OS_queue_table[*queue_id].original_name, (char*) queue_name);
	strcpy(OS_queue_table[*queue_id].name, (char*) os_unique_name);
	OS_queue_table[*queue_id].creator = OS_FindCreator();
	queue_count++;
	xSemaphoreGive(OS_queue_table_sem);

	return OS_SUCCESS;
} /* end OS_QueueCreate */

/*--------------------------------------------------------------------------------------
 Name: OS_QueueDelete

 Purpose: Deletes the specified message queue.

 Returns: OS_ERR_INVALID_ID if the id passed in does not exist
 OS_ERROR if the OS call to delete the queue fails
 OS_SUCCESS if success

 Notes: If There are messages on the queue, they will be lost and any subsequent
 calls to QueueGet or QueuePut to this queue will result in errors
 ---------------------------------------------------------------------------------------*/
int32 OS_QueueDelete(uint32 queue_id) {
	/*
	 * Note: There is currently no semaphore protection for the simple
	 * OS_queue_table accesses in this function, only the significant
	 * table entry update.
	 */
	/* Check to see if the queue_id given is valid */
	if (queue_id >= OS_QUEUE_TABLE_SIZE || OS_queue_table[queue_id].free == TRUE || OS_queue_table[queue_id].deleted == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	/* Try to delete the queue */
	vQueueDelete(OS_queue_table[queue_id].id);

	/*
	 * Now that the queue is deleted, remove its "presence"
	 * in OS_message_q_table and OS_message_q_name_table
	 */
	/*
	 ** Lock
	 */
	xSemaphoreTake(OS_queue_table_sem, portMAX_DELAY);

	OS_queue_table[queue_id].free = FALSE;
	OS_queue_table[queue_id].deleted = TRUE;
	OS_queue_table[queue_id].name[0] = '\0';
	OS_queue_table[queue_id].creator = UNINITIALIZED;
	OS_queue_table[queue_id].id = (SemaphoreHandle_t)0xFFFF;
	OS_queue_table[queue_id].max_size = 0;
	queue_count--;
	xSemaphoreGive(OS_queue_table_sem);

	return OS_SUCCESS;

} /* end OS_QueueDelete */

/*---------------------------------------------------------------------------------------
 Name: OS_QueueGet

 Purpose: Receive a message on a message queue.  Will pend or timeout on the receive.
 Returns: OS_ERR_INVALID_ID if the given ID does not exist
 OS_INVALID_POINTER if a pointer passed in is NULL
 OS_QUEUE_EMPTY if the Queue has no messages on it to be recieved
 OS_QUEUE_TIMEOUT if the timeout was OS_PEND and the time expired
 OS_QUEUE_INVALID_SIZE if the size passed in may be too small for the message
 OS_SUCCESS if success
 ---------------------------------------------------------------------------------------*/

int32 OS_QueueGet(uint32 queue_id, void *data, uint32 size, uint32 *size_copied,
		int32 timeout) {
	/* msecs rounded to the closest system tick count */
	BaseType_t status;
	TickType_t ticks;
	QueueHandle_t os_queue_id;

	/* Check Parameters */
	if (queue_id >= OS_QUEUE_TABLE_SIZE || OS_queue_table[queue_id].free == TRUE) {
		return (OS_ERR_INVALID_ID);
	} else if ((data == NULL) || (size_copied == NULL)) {
		return (OS_INVALID_POINTER);
	} else if (size < OS_queue_table[queue_id].max_size) {
		*size_copied = 0;
		return (OS_QUEUE_INVALID_SIZE);
	}

	os_queue_id = OS_queue_table[queue_id].id;

	/* Get Message From Message Queue */
	if (timeout == OS_PEND) {
		/*
		 ** Pend forever until a message arrives.
		 */
		status = xQueueReceive(os_queue_id, data, portMAX_DELAY);
		if (status == pdTRUE) {
			*size_copied = OS_queue_table[queue_id].max_size;
		} else {
			*size_copied = 0;
		}
	} else if (timeout == OS_CHECK) {
		/*
		 ** Get a message without waiting.  If no message is present,
		 ** return with a failure indication.
		 */
		status = xQueueReceive(os_queue_id, data, 0);
		if (status == pdTRUE) {
			*size_copied = OS_queue_table[queue_id].max_size;
		} else {
			*size_copied = 0;
			return OS_QUEUE_EMPTY;
		}
	} else {
		/*
		 ** Wait for up to a specified amount of time for a message to arrive.
		 ** If no message arrives within the timeout interval, return with a
		 ** failure indication.
		 */
		ticks = OS_Milli2Ticks(timeout);

		status = xQueueReceive(os_queue_id, data, ticks);
		if (status == pdTRUE) {
			*size_copied = OS_queue_table[queue_id].max_size;
		} else {
			*size_copied = 0;
			return OS_QUEUE_TIMEOUT;
		}

	}/* else */

	/*
	 ** Check the status of the read operation.  If a valid message was
	 ** obtained, indicate success.
	 */
	if (status == pdTRUE) {
		/* Success. */
		return OS_SUCCESS;
	} else {
		*size_copied = 0;
		return OS_ERROR;
	}

}/* end OS_QueueGet */

/*---------------------------------------------------------------------------------------
 Name: OS_QueuePut

 Purpose: Put a message on a message queue.

 Returns: OS_ERR_INVALID_ID if the queue id passed in is not a valid queue
 OS_INVALID_POINTER if the data pointer is NULL
 OS_QUEUE_FULL if the queue cannot accept another message
 OS_ERROR if the OS call returns an error
 OS_SUCCESS if SUCCESS

 Notes: The flags parameter is not used.  The message put is always configured to
 immediately return an error if the receiving message queue is full.
 ---------------------------------------------------------------------------------------*/

int32 OS_QueuePut(uint32 queue_id, const void *data, uint32 size, uint32 flags) {
	BaseType_t status;
	QueueHandle_t os_queue_id;

	/* Check Parameters */
	if (queue_id >= OS_QUEUE_TABLE_SIZE || OS_queue_table[queue_id].free == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	if (data == NULL) {
		return OS_INVALID_POINTER;
	}

	os_queue_id = OS_queue_table[queue_id].id;

	/* Get Message From Message Queue */

	/* Write the buffer pointer to the queue.  If an error occurred, report it
	 ** with the corresponding SB status code.
	 */
	status = xQueueSend(os_queue_id, data, 0);

	if (status == pdTRUE) {
		return OS_SUCCESS;
	} else if (status == errQUEUE_FULL) {
		/*
		 ** Queue is full.
		 */
		return OS_QUEUE_FULL;
	} else {
		/*
		 ** Unexpected error while writing to queue.
		 */
		return OS_ERROR;
	}
}/* end OS_QueuePut */

/*--------------------------------------------------------------------------------------
 Name: OS_QueueGetIdByName

 Purpose: This function tries to find a queue Id given the name of the queue. The
 id of the queue is passed back in queue_id

 Returns: OS_INVALID_POINTER if the name or id pointers are NULL
 OS_ERR_NAME_TOO_LONG the name passed in is too long
 OS_ERR_NAME_NOT_FOUND the name was not found in the table
 OS_SUCCESS if success

 ---------------------------------------------------------------------------------------*/

int32 OS_QueueGetIdByName(uint32 *queue_id, const char *queue_name) {
	uint32 i;

	if (queue_id == NULL || queue_name == NULL) {
		return OS_INVALID_POINTER;
	}

	/* a name too long wouldn't have been allowed in the first place
	 * so we definitely won't find a name too long*/
	if (strlen(queue_name) >= MAX_API_NAME_INCOMING) {
		return OS_ERR_NAME_TOO_LONG;
	}

	for (i = 0; i < OS_QUEUE_TABLE_SIZE; i++) {
		if (OS_queue_table[i].free != TRUE && OS_queue_table[i].deleted == FALSE
				&& (strcmp(OS_queue_table[i].original_name, (char*) queue_name) == 0)) {
			*queue_id = i;
			return OS_SUCCESS;
		}
	}

	/*
	 ** The name was not found in the table,
	 ** or it was, and the queue_id isn't valid anymore
	 */
	return OS_ERR_NAME_NOT_FOUND;

}/* end OS_QueueGetIdByName */

/*---------------------------------------------------------------------------------------
 Name: OS_QueueGetInfo

 Purpose: This function will pass back a pointer to structure that contains
 all of the relevant info (name and creator) about the specified queue.

 Returns: OS_INVALID_POINTER if queue_prop is NULL
 OS_ERR_INVALID_ID if the ID given is not  a valid queue
 OS_SUCCESS if the info was copied over correctly
 ---------------------------------------------------------------------------------------*/

int32 OS_QueueGetInfo(uint32 queue_id, OS_queue_prop_t *queue_prop) {
	/* Check to see that the id given is valid */
	if (queue_prop == NULL) {
		return OS_INVALID_POINTER;
	}

	if (queue_id >= OS_QUEUE_TABLE_SIZE || OS_queue_table[queue_id].free == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	/* put the info into the stucture */
	xSemaphoreTake(OS_queue_table_sem, portMAX_DELAY);
	queue_prop->creator = OS_queue_table[queue_id].creator;
	strcpy(queue_prop->name, OS_queue_table[queue_id].original_name);
	xSemaphoreGive(OS_queue_table_sem);

	return OS_SUCCESS;
} /* end OS_QueueGetInfo */
/****************************************************************************************
 SEMAPHORE API
 ****************************************************************************************/

/*---------------------------------------------------------------------------------------
 Name: OS_BinSemCreate

 Purpose: Creates a binary semaphore with initial value specified by
 sem_initial_value and name specified by sem_name. sem_id will be
 returned to the caller

 Returns: OS_INVALID_POINTER if sen name or sem_id are NULL
 OS_ERR_NAME_TOO_LONG if the name given is too long
 OS_ERR_NO_FREE_IDS if all of the semaphore ids are taken
 OS_ERR_NAME_TAKEN if this is already the name of a binary semaphore
 OS_SEM_FAILURE if the OS call failed
 OS_SUCCESS if success


 Notes: options is an unused parameter
 ---------------------------------------------------------------------------------------*/

int32 OS_BinSemCreate(uint32 *sem_id, const char *sem_name,
		uint32 sem_initial_value, uint32 options) {
	uint32 possible_semid;
	uint32 i;

	if (sem_id == NULL || sem_name == NULL) {
		return OS_INVALID_POINTER;
	}

	char os_unique_name[OS_MAX_API_NAME];
	snprintf(os_unique_name, OS_MAX_API_NAME, "%-*.*s%02x", (OS_MAX_API_NAME-3), (OS_MAX_API_NAME-3), sem_name, name_counter++);

	/* we don't want to allow names too long*/
	/* if truncated, two names might be the same */
	if (strlen(sem_name) >= MAX_API_NAME_INCOMING) {
		return OS_ERR_NAME_TOO_LONG;
	}
	if (strlen(os_unique_name) >= OS_MAX_API_NAME) {
		return OS_ERR_NAME_TOO_LONG;
	}

	/* Check Parameters */
	xSemaphoreTake(OS_bin_sem_table_sem, portMAX_DELAY);

	/* Ensure a free resource is available */
	if (bin_sem_count >= OS_MAX_BIN_SEMAPHORES) {
		xSemaphoreGive(OS_bin_sem_table_sem);
		return OS_ERR_NO_FREE_IDS;
	}

	for (possible_semid = 0; possible_semid < OS_BIN_SEM_TABLE_SIZE;
			possible_semid++) {
		if (OS_bin_sem_table[possible_semid].free == TRUE)
			break;
	}

	if ((possible_semid >= OS_BIN_SEM_TABLE_SIZE)
			|| (OS_bin_sem_table[possible_semid].free != TRUE)) {
		xSemaphoreGive(OS_bin_sem_table_sem);
		return OS_ERR_NO_FREE_IDS;
	}

	/* Check to see if the name is already taken */
	for (i = 0; i < OS_BIN_SEM_TABLE_SIZE; i++) {
		if ((OS_bin_sem_table[i].free == FALSE && OS_bin_sem_table[i].deleted == FALSE)
				&& strcmp((char*) sem_name, OS_bin_sem_table[i].original_name) == 0) {
			xSemaphoreGive(OS_bin_sem_table_sem);
			return OS_ERR_NAME_TAKEN;
		}
	}
	OS_bin_sem_table[possible_semid].free = FALSE;
	xSemaphoreGive(OS_bin_sem_table_sem);

	/* Check to make sure the sem value is going to be either 0 or 1 */
	if (sem_initial_value > 1) {
		sem_initial_value = 1;
	}

	/* Create Semaphore */
	OS_bin_sem_table[possible_semid].id = xSemaphoreCreateBinary();

	/* check if Create failed */
	if (OS_bin_sem_table[possible_semid].id == NULL) {
		xSemaphoreTake(OS_bin_sem_table_sem, portMAX_DELAY);
		OS_bin_sem_table[possible_semid].free = TRUE;
		OS_bin_sem_table[possible_semid].id = 0;
		xSemaphoreGive(OS_bin_sem_table_sem);
		return OS_SEM_FAILURE;
	}

	// Initialize the semaphore value
	for (i = 0; i < sem_initial_value; ++i) {
		xSemaphoreGive(OS_bin_sem_table[possible_semid].id);
	}

	/* Set the sem_id to the one that we found available */
	/* Set the name of the semaphore,creator and free as well */

	*sem_id = possible_semid;

	xSemaphoreTake(OS_bin_sem_table_sem, portMAX_DELAY);
	OS_bin_sem_table[*sem_id].free = FALSE;
	strcpy(OS_bin_sem_table[*sem_id].original_name, (char*) sem_name);
	strcpy(OS_bin_sem_table[*sem_id].name, (char*) os_unique_name);
	OS_bin_sem_table[*sem_id].creator = OS_FindCreator();
	bin_sem_count++;
	xSemaphoreGive(OS_bin_sem_table_sem);

	return OS_SUCCESS;
}/* end OS_BinSemCreate */

/*--------------------------------------------------------------------------------------
 Name: OS_BinSemDelete

 Purpose: Deletes the specified Binary Semaphore.

 Returns: OS_ERR_INVALID_ID if the id passed in is not a valid binary semaphore
 OS_SEM_FAILURE the OS call failed
 OS_SUCCESS if success

 ---------------------------------------------------------------------------------------*/

int32 OS_BinSemDelete(uint32 sem_id) {

	/* Check to see if this sem_id is valid */
	if (sem_id >= OS_BIN_SEM_TABLE_SIZE || OS_bin_sem_table[sem_id].free == TRUE
			|| OS_bin_sem_table[sem_id].deleted == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	/* we must make sure the semaphore is given  to delete it */
	xSemaphoreGive(OS_bin_sem_table[sem_id].id);

	vSemaphoreDelete(OS_bin_sem_table[sem_id].id);

	/* Remove the Id from the table, and its name, so that it cannot be found again */
	xSemaphoreTake(OS_bin_sem_table_sem, portMAX_DELAY);
	OS_bin_sem_table[sem_id].free = FALSE;
	OS_bin_sem_table[sem_id].deleted = TRUE;
	OS_bin_sem_table[sem_id].name[0] = '\0';
	OS_bin_sem_table[sem_id].creator = UNINITIALIZED;
	OS_bin_sem_table[sem_id].id = (SemaphoreHandle_t)0xFFFF;
	bin_sem_count--;
	xSemaphoreGive(OS_bin_sem_table_sem);

	return OS_SUCCESS;
}/* end OS_BinSemDelete */

/*---------------------------------------------------------------------------------------
 Name: OS_BinSemGive

 Purpose: The function  unlocks the semaphore referenced by sem_id by performing
 a semaphore unlock operation on that semaphore.If the semaphore value
 resulting from this operation is positive, then no threads were blocked
 waiting for the semaphore to become unlocked; the semaphore value is
 simply incremented for this semaphore.


 Returns: OS_SEM_FAILURE the semaphore was not previously  initialized or is not
 in the array of semaphores defined by the system
 OS_ERR_INVALID_ID if the id passed in is not a binary semaphore
 OS_SUCCESS if success

 ---------------------------------------------------------------------------------------*/

int32 OS_BinSemGive(uint32 sem_id) {
	/* Check Parameters */
	if (sem_id >= OS_BIN_SEM_TABLE_SIZE || OS_bin_sem_table[sem_id].free == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	xSemaphoreGive(OS_bin_sem_table[sem_id].id);
	return OS_SUCCESS;
}/* end OS_BinSemGive */

/*---------------------------------------------------------------------------------------
 Name: OS_BinSemFlush

 Purpose: The function  releases all the tasks pending on this semaphore. Note
 that the state of the semaphore is not changed by this operation.

 Returns: OS_SEM_FAILURE the semaphore was not previously  initialized or is not
 in the array of semaphores defined by the system
 OS_ERR_INVALID_ID if the id passed in is not a binary semaphore
 OS_SUCCESS if success

 ---------------------------------------------------------------------------------------*/

int32 OS_BinSemFlush(uint32 sem_id) {
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_ERR_NOT_IMPLEMENTED;
}/* end OS_BinSemFlush */

/*---------------------------------------------------------------------------------------
 Name:    OS_BinSemTake

 Purpose: The locks the semaphore referenced by sem_id by performing a
 semaphore lock operation on that semaphore.If the semaphore value
 is currently zero, then the calling thread shall not return from
 the call until it either locks the semaphore or the call is
 interrupted by a signal.

 Return:  OS_ERR_INVALID_ID : the semaphore was not previously initialized
 or is not in the array of semaphores defined by the system
 OS_SEM_FAILURE if the OS call failed and the semaphore is not obtained
 OS_SUCCESS if success

 ----------------------------------------------------------------------------------------*/

int32 OS_BinSemTake(uint32 sem_id) {
	BaseType_t status;

	/* Check Parameters */
	if (sem_id >= OS_BIN_SEM_TABLE_SIZE || OS_bin_sem_table[sem_id].free == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	status = xSemaphoreTake(OS_bin_sem_table[sem_id].id, portMAX_DELAY);
	if (status == pdTRUE) {
		return OS_SUCCESS;
	} else {
		return OS_SEM_FAILURE;
	}

}/* end OS_BinSemTake */
/*---------------------------------------------------------------------------------------
 Name: OS_BinSemTimedWait

 Purpose: The function locks the semaphore referenced by sem_id . However,
 if the semaphore cannot be locked without waiting for another process
 or thread to unlock the semaphore , this wait shall be terminated when
 the specified timeout ,msecs, expires.

 Returns: OS_SEM_TIMEOUT if semaphore was not relinquished in time
 OS_SUCCESS if success
 OS_SEM_FAILURE the semaphore was not previously initialized or is not
 in the array of semaphores defined by the system
 OS_ERR_INVALID_ID if the ID passed in is not a valid semaphore ID
 ----------------------------------------------------------------------------------------*/

int32 OS_BinSemTimedWait(uint32 sem_id, uint32 msecs) {
	BaseType_t status;
	uint32 TimeInTicks;

	/* Check Parameters */
	if ((sem_id >= OS_BIN_SEM_TABLE_SIZE)
			|| (OS_bin_sem_table[sem_id].free == TRUE)) {
		return OS_ERR_INVALID_ID;
	}

	TimeInTicks = OS_Milli2Ticks(msecs);

	status = xSemaphoreTake(OS_bin_sem_table[sem_id].id, TimeInTicks);

	switch (status) {
	case pdFALSE:

		status = OS_SEM_TIMEOUT;
		break;

	case pdTRUE:
		status = OS_SUCCESS;
		break;

	default:
		status = OS_SEM_FAILURE;
		break;
	}
	return status;

}/* end OS_BinSemTimedWait */

/*--------------------------------------------------------------------------------------
 Name: OS_BinSemGetIdByName

 Purpose: This function tries to find a binary sem Id given the name of a bin_sem
 The id is returned through sem_id

 Returns: OS_INVALID_POINTER is semid or sem_name are NULL pointers
 OS_ERR_NAME_TOO_LONG if the name given is to long to have been stored
 OS_ERR_NAME_NOT_FOUND if the name was not found in the table
 OS_SUCCESS if success

 ---------------------------------------------------------------------------------------*/
int32 OS_BinSemGetIdByName(uint32 *sem_id, const char *sem_name) {
	uint32 i;

	if (sem_id == NULL || sem_name == NULL) {
		return OS_INVALID_POINTER;
	}

	/*
	 ** a name too long wouldn't have been allowed in the first place
	 ** so we definitely won't find a name too long
	 */
	if (strlen(sem_name) >= MAX_API_NAME_INCOMING) {
		return OS_ERR_NAME_TOO_LONG;
	}

	for (i = 0; i < OS_BIN_SEM_TABLE_SIZE; i++) {
		if (OS_bin_sem_table[i].free != TRUE && OS_bin_sem_table[i].deleted == FALSE
				&& (strcmp(OS_bin_sem_table[i].original_name, (char*) sem_name) == 0)) {
			*sem_id = i;
			return OS_SUCCESS;
		}
	}
	/*
	 ** The name was not found in the table,
	 ** or it was, and the sem_id isn't valid anymore
	 */
	return OS_ERR_NAME_NOT_FOUND;

}/* end OS_BinSemGetIdByName */
/*---------------------------------------------------------------------------------------
 Name: OS_BinSemGetInfo

 Purpose: This function will pass back a pointer to structure that contains
 all of the relevant info( name and creator) about the specified binary
 semaphore.

 Returns: OS_ERR_INVALID_ID if the id passed in is not a valid semaphore
 OS_INVALID_POINTER if the bin_prop pointer is null
 OS_SUCCESS if success
 ---------------------------------------------------------------------------------------*/

int32 OS_BinSemGetInfo(uint32 sem_id, OS_bin_sem_prop_t *bin_prop) {

	/* Check to see that the id given is valid */
	if (sem_id >= OS_BIN_SEM_TABLE_SIZE || OS_bin_sem_table[sem_id].free == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	if (bin_prop == NULL) {
		return OS_INVALID_POINTER;
	}

	/* put the info into the stucture */
	xSemaphoreTake(OS_bin_sem_table_sem, portMAX_DELAY);

	bin_prop->creator = OS_bin_sem_table[sem_id].creator;
	strcpy(bin_prop->name, OS_bin_sem_table[sem_id].original_name);
	bin_prop->value = 0;

	xSemaphoreGive(OS_bin_sem_table_sem);

	return OS_SUCCESS;

} /* end OS_BinSemGetInfo */
/*---------------------------------------------------------------------------------------
 Name: OS_CountSemCreate

 Purpose: Creates a countary semaphore with initial value specified by
 sem_initial_value and name specified by sem_name. sem_id will be
 returned to the caller

 Returns: OS_INVALID_POINTER if sen name or sem_id are NULL
 OS_ERR_NAME_TOO_LONG if the name given is too long
 OS_ERR_NO_FREE_IDS if all of the semaphore ids are taken
 OS_ERR_NAME_TAKEN if this is already the name of a countary semaphore
 OS_SEM_FAILURE if the OS call failed
 OS_SUCCESS if success

 Notes: options is an unused parameter
 ---------------------------------------------------------------------------------------*/

int32 OS_CountSemCreate(uint32 *sem_id, const char *sem_name,
		uint32 sem_initial_value, uint32 options) {
	uint32 possible_semid;
	uint32 i;

	/*
	 ** Check Parameters
	 */
	if (sem_id == NULL || sem_name == NULL) {
		return OS_INVALID_POINTER;
	}

	/*
	 ** Verify that the semaphore maximum value is not too high
	 */
	if (sem_initial_value > MAX_SEM_VALUE) {
		return OS_INVALID_SEM_VALUE;
	}

	char os_unique_name[OS_MAX_API_NAME];
	snprintf(os_unique_name, OS_MAX_API_NAME, "%-*.*s%02x", (OS_MAX_API_NAME-3), (OS_MAX_API_NAME-3), sem_name, name_counter++);

	/*
	 **  we don't want to allow names too long
	 ** if truncated, two names might be the same
	 */
	if (strlen(sem_name) >= MAX_API_NAME_INCOMING) {
		return OS_ERR_NAME_TOO_LONG;
	}
	if (strlen(os_unique_name) >= OS_MAX_API_NAME) {
		return OS_ERR_NAME_TOO_LONG;
	}

	/*
	 ** Lock
	 */
	xSemaphoreTake(OS_count_sem_table_sem, portMAX_DELAY);

	for (possible_semid = 0; possible_semid < OS_MAX_COUNT_SEMAPHORES;
			possible_semid++) {
		if (OS_count_sem_table[possible_semid].free == TRUE)
			break;
	}

	if ((possible_semid >= OS_MAX_COUNT_SEMAPHORES)
			|| (OS_count_sem_table[possible_semid].free != TRUE)) {
		xSemaphoreGive(OS_count_sem_table_sem);
		return OS_ERR_NO_FREE_IDS;
	}

	/* Check to see if the name is already taken */
	for (i = 0; i < OS_MAX_COUNT_SEMAPHORES; i++) {
		if ((OS_count_sem_table[i].free == FALSE)
				&& strcmp((char*) sem_name, OS_count_sem_table[i].original_name) == 0) {
			xSemaphoreGive(OS_count_sem_table_sem);
			return OS_ERR_NAME_TAKEN;
		}
	}
	OS_count_sem_table[possible_semid].free = FALSE;
	xSemaphoreGive(OS_count_sem_table_sem);

	OS_count_sem_table[possible_semid].id = xSemaphoreCreateCounting(
			MAX_SEM_VALUE, sem_initial_value);

	/* check if Create failed */
	if (OS_count_sem_table[possible_semid].id == NULL) {
		xSemaphoreTake(OS_count_sem_table_sem, portMAX_DELAY);
		OS_count_sem_table[possible_semid].free = TRUE;
		xSemaphoreGive(OS_count_sem_table_sem);

		return OS_SEM_FAILURE;
	}
	/* Set the sem_id to the one that we found available */
	/* Set the name of the semaphore,creator and free as well */

	*sem_id = possible_semid;

	xSemaphoreTake(OS_count_sem_table_sem, portMAX_DELAY);
	OS_count_sem_table[*sem_id].free = FALSE;
	strcpy(OS_count_sem_table[*sem_id].original_name, (char*) sem_name);
	strcpy(OS_count_sem_table[*sem_id].name, (char*) os_unique_name);
	OS_count_sem_table[*sem_id].creator = OS_FindCreator();

	/*
	 ** Unlock
	 */
	xSemaphoreGive(OS_count_sem_table_sem);

	return OS_SUCCESS;

}/* end OS_CountSemCreate */

/*--------------------------------------------------------------------------------------
 Name: OS_CountSemDelete

 Purpose: Deletes the specified Counting Semaphore.

 Returns: OS_ERR_INVALID_ID if the id passed in is not a valid countary semaphore
 OS_SEM_FAILURE the OS call failed
 OS_SUCCESS if success

 ---------------------------------------------------------------------------------------*/

int32 OS_CountSemDelete(uint32 sem_id) {
	/*
	 ** Check to see if this sem_id is valid
	 */
	if (sem_id >= OS_MAX_COUNT_SEMAPHORES
			|| OS_count_sem_table[sem_id].free == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	/* we must make sure the semaphore is given  to delete it */
	xSemaphoreGive(OS_count_sem_table[sem_id].id);

	vSemaphoreDelete(OS_count_sem_table[sem_id].id);

	/* Remove the Id from the table, and its name, so that it cannot be found again */
	xSemaphoreTake(OS_count_sem_table_sem, portMAX_DELAY);

	OS_count_sem_table[sem_id].free = TRUE;
	OS_count_sem_table[sem_id].name[0] = '\0';
	OS_count_sem_table[sem_id].creator = UNINITIALIZED;
	OS_count_sem_table[sem_id].id = UNINITIALIZED;

	xSemaphoreGive(OS_count_sem_table_sem);

	return OS_SUCCESS;

}/* end OS_CountSemDelete */

/*---------------------------------------------------------------------------------------
 Name: OS_CountSemGive

 Purpose: The function  unlocks the semaphore referenced by sem_id by performing
 a semaphore unlock operation on that semaphore.If the semaphore value
 resulting from this operation is positive, then no threads were blocked
 waiting for the semaphore to become unlocked; the semaphore value is
 simply incremented for this semaphore.


 Returns: OS_SEM_FAILURE the semaphore was not previously  initialized or is not
 in the array of semaphores defined by the system
 OS_ERR_INVALID_ID if the id passed in is not a countary semaphore
 OS_SUCCESS if success

 ---------------------------------------------------------------------------------------*/

int32 OS_CountSemGive(uint32 sem_id) {
	int32 return_code = OS_SUCCESS;

	/*
	 ** Check Parameters
	 */
	if (sem_id >= OS_MAX_COUNT_SEMAPHORES
			|| OS_count_sem_table[sem_id].free == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	if (xSemaphoreGive(OS_count_sem_table[sem_id].id) != pdTRUE) {
		return_code = OS_SEM_FAILURE;
	} else {
		return_code = OS_SUCCESS;
	}
	return (return_code);
}/* end OS_CountSemGive */

/*---------------------------------------------------------------------------------------
 Name:    OS_CountSemTake

 Purpose: The locks the semaphore referenced by sem_id by performing a
 semaphore lock operation on that semaphore.If the semaphore value
 is currently zero, then the calling thread shall not return from
 the call until it either locks the semaphore or the call is
 interrupted by a signal.

 Return:  OS_SEM_FAILURE : the semaphore was not previously initialized
 or is not in the array of semaphores defined by the system
 OS_ERR_INVALID_ID the Id passed in is not a valid countar semaphore
 OS_SEM_FAILURE if the OS call failed
 OS_SUCCESS if success

 ----------------------------------------------------------------------------------------*/

int32 OS_CountSemTake(uint32 sem_id) {
	int32 return_code = OS_SUCCESS;

	/* Check Parameters */
	if (sem_id >= OS_MAX_COUNT_SEMAPHORES
			|| OS_count_sem_table[sem_id].free == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	if (xSemaphoreTake(OS_count_sem_table[sem_id].id, portMAX_DELAY) != pdTRUE) {
		return_code = OS_SEM_FAILURE;
	}
	return (return_code);
}/* end OS_CountSemTake */

/*---------------------------------------------------------------------------------------
 Name: OS_CountSemTimedWait

 Purpose: The function locks the semaphore referenced by sem_id . However,
 if the semaphore cannot be locked without waiting for another process
 or thread to unlock the semaphore , this wait shall be terminated when
 the specified timeout ,msecs, expires.

 Returns: OS_SEM_TIMEOUT if semaphore was not relinquished in time
 OS_SUCCESS if success
 OS_SEM_FAILURE the semaphore was not previously initialized or is not
 in the array of semaphores defined by the system
 OS_ERR_INVALID_ID if the ID passed in is not a valid semaphore ID
 ----------------------------------------------------------------------------------------*/

int32 OS_CountSemTimedWait(uint32 sem_id, uint32 msecs) {
	BaseType_t status;
	int32 return_code = OS_SUCCESS;
	uint32 TimeInTicks;

	/* Check Parameters */
	if ((sem_id >= OS_MAX_COUNT_SEMAPHORES)
			|| (OS_count_sem_table[sem_id].free == TRUE)) {
		return OS_ERR_INVALID_ID;
	}

	TimeInTicks = OS_Milli2Ticks(msecs);

	status = xSemaphoreTake(OS_count_sem_table[sem_id].id, TimeInTicks);
	switch (status) {
	case pdFALSE:
		return_code = OS_SEM_TIMEOUT;
		break;

	case pdTRUE:
		return_code = OS_SUCCESS;
		break;

	default:
		return_code = OS_SEM_FAILURE;
		break;
	}
	return (return_code);

}/* end OS_CountSemTimedWait */

/*--------------------------------------------------------------------------------------
 Name: OS_CountSemGetIdByName

 Purpose: This function tries to find a countary sem Id given the name of a count_sem
 The id is returned through sem_id

 Returns: OS_INVALID_POINTER is semid or sem_name are NULL pointers
 OS_ERR_NAME_TOO_LONG if the name given is to long to have been stored
 OS_ERR_NAME_NOT_FOUND if the name was not found in the table
 OS_SUCCESS if success

 ---------------------------------------------------------------------------------------*/
int32 OS_CountSemGetIdByName(uint32 *sem_id, const char *sem_name) {
	uint32 i;

	if (sem_id == NULL || sem_name == NULL) {
		return OS_INVALID_POINTER;
	}

	/*
	 ** a name too long wouldn't have been allowed in the first place
	 ** so we definitely won't find a name too long
	 */
	if (strlen(sem_name) >= MAX_API_NAME_INCOMING) {
		return OS_ERR_NAME_TOO_LONG;
	}

	for (i = 0; i < OS_MAX_COUNT_SEMAPHORES; i++) {
		if (OS_count_sem_table[i].free != TRUE
				&& (strcmp(OS_count_sem_table[i].original_name, (char*) sem_name) == 0)) {
			*sem_id = i;
			return OS_SUCCESS;
		}
	}

	/*
	 ** The name was not found in the table,
	 **  or it was, and the sem_id isn't valid anymore
	 */
	return OS_ERR_NAME_NOT_FOUND;

}/* end OS_CountSemGetIdByName */
/*---------------------------------------------------------------------------------------
 Name: OS_CountSemGetInfo

 Purpose: This function will pass back a pointer to structure that contains
 all of the relevant info( name and creator) about the specified countary
 semaphore.

 Returns: OS_ERR_INVALID_ID if the id passed in is not a valid semaphore
 OS_INVALID_POINTER if the count_prop pointer is null
 OS_SUCCESS if success
 ---------------------------------------------------------------------------------------*/

int32 OS_CountSemGetInfo(uint32 sem_id, OS_count_sem_prop_t *count_prop) {

	/* Check to see that the id given is valid */
	if (sem_id >= OS_MAX_COUNT_SEMAPHORES
			|| OS_count_sem_table[sem_id].free == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	if (count_prop == NULL) {
		return OS_INVALID_POINTER;
	}

	/*
	 ** Lock
	 */
	xSemaphoreTake(OS_count_sem_table_sem, portMAX_DELAY);

	/*
	 ** Populate the info stucture
	 */
	count_prop->creator = OS_count_sem_table[sem_id].creator;
	strcpy(count_prop->name, OS_count_sem_table[sem_id].original_name);
	count_prop->value = 0;

	/*
	 ** Unlock
	 */
	xSemaphoreGive(OS_count_sem_table_sem);

	return OS_SUCCESS;

} /* end OS_CountSemGetInfo */

/****************************************************************************************
 MUTEX API
 ****************************************************************************************/

/*---------------------------------------------------------------------------------------
 Name: OS_MutSemCreate

 Purpose: Creates a mutex semaphore initially full.

 Returns: OS_INVALID_POINTER if sem_id or sem_name are NULL
 OS_ERR_NAME_TOO_LONG if the sem_name is too long to be stored
 OS_ERR_NO_FREE_IDS if there are no more free mutex Ids
 OS_ERR_NAME_TAKEN if there is already a mutex with the same name
 OS_SEM_FAILURE if the OS call failed
 OS_SUCCESS if success

 Notes: the options parameter is not used in this implementation

 ---------------------------------------------------------------------------------------*/

int32 OS_MutSemCreate(uint32 *sem_id, const char *sem_name, uint32 options) {
	uint32 possible_semid;
	uint32 i;

	/* Check Parameters */
	if (sem_id == NULL || sem_name == NULL) {
		return OS_INVALID_POINTER;
	}

	char os_unique_name[OS_MAX_API_NAME];
	snprintf(os_unique_name, OS_MAX_API_NAME, "%-*.*s%02x", (OS_MAX_API_NAME-3), (OS_MAX_API_NAME-3), sem_name, name_counter++);

	/*
	 ** we don't want to allow names too long
	 ** if truncated, two names might be the same
	 */
	if (strlen(sem_name) >= MAX_API_NAME_INCOMING) {
		return OS_ERR_NAME_TOO_LONG;
	}
	if (strlen(os_unique_name) >= OS_MAX_API_NAME) {
		return OS_ERR_NAME_TOO_LONG;
	}

	xSemaphoreTake(OS_mut_sem_table_sem, portMAX_DELAY);

	/* Ensure a free resource is available */
	if (mut_sem_count >= OS_MAX_MUTEXES) {
		xSemaphoreGive(OS_mut_sem_table_sem);
		return OS_ERR_NO_FREE_IDS;
	}

	for (possible_semid = 0; possible_semid < OS_MUTEX_TABLE_SIZE;
			possible_semid++) {
		if (OS_mut_sem_table[possible_semid].free == TRUE)
			break;
	}

	if ((possible_semid >= OS_MUTEX_TABLE_SIZE)
			|| (OS_mut_sem_table[possible_semid].free != TRUE)) {
		xSemaphoreGive(OS_mut_sem_table_sem);
		return OS_ERR_NO_FREE_IDS;
	}

	/* Check to see if the name is already taken */
	for (i = 0; i < OS_MUTEX_TABLE_SIZE; i++) {
		if ((OS_mut_sem_table[i].free == FALSE) && (OS_mut_sem_table[i].deleted == FALSE)
				&& strcmp((char*) sem_name, OS_mut_sem_table[i].original_name) == 0) {
			xSemaphoreGive(OS_mut_sem_table_sem);
			return OS_ERR_NAME_TAKEN;
		}
	}

	OS_mut_sem_table[possible_semid].free = FALSE;
	xSemaphoreGive(OS_mut_sem_table_sem);

	/*
	 ** Try to create the mutex
	 */
	OS_mut_sem_table[possible_semid].id = xSemaphoreCreateRecursiveMutex();
	if (OS_mut_sem_table[possible_semid].id == NULL) {
		xSemaphoreTake(OS_mut_sem_table_sem, portMAX_DELAY);
		OS_mut_sem_table[possible_semid].free = TRUE;
		xSemaphoreGive(OS_mut_sem_table_sem);
		return OS_SEM_FAILURE;
	}

	*sem_id = possible_semid;

	xSemaphoreTake(OS_mut_sem_table_sem, portMAX_DELAY);
	strcpy(OS_mut_sem_table[*sem_id].original_name, (char*) sem_name);
	strcpy(OS_mut_sem_table[*sem_id].name, (char*) os_unique_name);
	OS_mut_sem_table[*sem_id].free = FALSE;
	OS_mut_sem_table[*sem_id].creator = OS_FindCreator();
	mut_sem_count++;
	xSemaphoreGive(OS_mut_sem_table_sem);

	return OS_SUCCESS;

}/* end OS_MutSemCreate */

/*--------------------------------------------------------------------------------------
 Name: OS_MutSemDelete

 Purpose: Deletes the specified Mutex Semaphore.

 Returns: OS_ERR_INVALID_ID if the id passed in is not a valid mutex
 OS_SEM_FAILURE if the OS call failed
 OS_SUCCESS if success

 ---------------------------------------------------------------------------------------*/

int32 OS_MutSemDelete(uint32 sem_id) {

	/* Check to see if this sem_id is valid   */
	if (sem_id >= OS_MUTEX_TABLE_SIZE || OS_mut_sem_table[sem_id].free == TRUE || OS_mut_sem_table[sem_id].deleted == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	vSemaphoreDelete(OS_mut_sem_table[sem_id].id);

	/* Delete its presence in the table */
	xSemaphoreTake(OS_mut_sem_table_sem, portMAX_DELAY);

	OS_mut_sem_table[sem_id].free = FALSE; // remain false
	OS_mut_sem_table[sem_id].deleted = TRUE;
	OS_mut_sem_table[sem_id].id = (SemaphoreHandle_t)0xFFFF;
	OS_mut_sem_table[sem_id].name[0] = '\0';
	OS_mut_sem_table[sem_id].creator = UNINITIALIZED;
	mut_sem_count--;
	xSemaphoreGive(OS_mut_sem_table_sem);

	return OS_SUCCESS;
}/* end OS_MutSemDelete */

/*---------------------------------------------------------------------------------------
 Name: OS_MutSemGive

 Purpose: The function releases the mutex object referenced by sem_id.The
 manner in which a mutex is released is dependent upon the mutex's type
 attribute.  If there are threads blocked on the mutex object referenced by
 mutex when this function is called, resulting in the mutex becoming
 available, the scheduling policy shall determine which thread shall
 acquire the mutex.

 Returns: OS_SUCCESS if success
 OS_SEM_FAILURE if the semaphore was not previously  initialized
 OS_ERR_INVALID_ID if the id passed in is not a valid mutex

 ---------------------------------------------------------------------------------------*/

int32 OS_MutSemGive(uint32 sem_id) {
	/* Check Parameters */
	if (sem_id >= OS_MUTEX_TABLE_SIZE || OS_mut_sem_table[sem_id].free == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	/* Give the mutex */
	if (xSemaphoreGiveRecursive(OS_mut_sem_table[sem_id].id) != pdTRUE) {
		return OS_SEM_FAILURE;
	} else {
		return OS_SUCCESS;
	}

}/* end OS_MutSemGive */

/*---------------------------------------------------------------------------------------
 Name: OS_MutSemTake

 Purpose: The mutex object referenced by sem_id shall be locked by calling this
 function. If the mutex is already locked, the calling thread shall
 block until the mutex becomes available. This operation shall return
 with the mutex object referenced by mutex in the locked state with the
 calling thread as its owner.

 Returns: OS_SUCCESS if success
 OS_SEM_FAILURE if the semaphore was not previously initialized or is
 not in the array of semaphores defined by the system
 OS_ERR_INVALID_ID the id passed in is not a valid mutex
 ---------------------------------------------------------------------------------------*/
int32 OS_MutSemTake(uint32 sem_id) {
	/*
	 ** Check Parameters
	 */
	if (sem_id >= OS_MUTEX_TABLE_SIZE || OS_mut_sem_table[sem_id].free == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	if ( xSemaphoreTakeRecursive(OS_mut_sem_table[sem_id].id,
			portMAX_DELAY) != pdTRUE) {
		return OS_SEM_FAILURE;
	} else {
		return OS_SUCCESS;
	}

}/* end OS_MutSemGive */

/*--------------------------------------------------------------------------------------
 Name: OS_MutSemGetIdByName

 Purpose: This function tries to find a mutex sem Id given the name of a bin_sem
 The id is returned through sem_id

 Returns: OS_INVALID_POINTER is semid or sem_name are NULL pointers
 OS_ERR_NAME_TOO_LONG if the name given is to long to have been stored
 OS_ERR_NAME_NOT_FOUND if the name was not found in the table
 OS_SUCCESS if success

 ---------------------------------------------------------------------------------------*/

int32 OS_MutSemGetIdByName(uint32 *sem_id, const char *sem_name) {
	uint32 i;

	if (sem_id == NULL || sem_name == NULL) {
		return OS_INVALID_POINTER;
	}

	/* a name too long wouldn't have been allowed in the first place
	 * so we definitely won't find a name too long*/
	if (strlen(sem_name) >= MAX_API_NAME_INCOMING) {
		return OS_ERR_NAME_TOO_LONG;
	}

	for (i = 0; i < OS_MUTEX_TABLE_SIZE; i++) {
		if ((OS_mut_sem_table[i].free != TRUE && OS_mut_sem_table[i].deleted == FALSE)
				&& (strcmp(OS_mut_sem_table[i].original_name, (char*) sem_name) == 0)) {
			*sem_id = i;
			return OS_SUCCESS;
		}
	}

	/* The name was not found in the table,
	 *  or it was, and the sem_id isn't valid anymore */

	return OS_ERR_NAME_NOT_FOUND;

}/* end OS_MutSemGetIdByName */
/*---------------------------------------------------------------------------------------
 Name: OS_MutSemGetInfo

 Purpose: This function will pass back a pointer to structure that contains
 all of the relevant info( name and creator) about the specified mutex
 semaphore.

 Returns: OS_ERR_INVALID_ID if the id passed in is not a valid semaphore
 OS_INVALID_POINTER if the mut_prop pointer is null
 OS_SUCCESS if success
 ---------------------------------------------------------------------------------------*/

int32 OS_MutSemGetInfo(uint32 sem_id, OS_mut_sem_prop_t *mut_prop) {
	/* Check to see that the id given is valid */
	if (sem_id >= OS_MUTEX_TABLE_SIZE || OS_mut_sem_table[sem_id].free == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	if (mut_prop == NULL) {
		return OS_INVALID_POINTER;
	}

	/* put the info into the stucture */
	xSemaphoreTake(OS_mut_sem_table_sem, portMAX_DELAY);
	mut_prop->creator = OS_mut_sem_table[sem_id].creator;
	strcpy(mut_prop->name, OS_mut_sem_table[sem_id].original_name);
	xSemaphoreGive(OS_mut_sem_table_sem);

	return OS_SUCCESS;

} /* end OS_MutSemGetInfo */

/****************************************************************************************
 TICK API
 ****************************************************************************************/

/*---------------------------------------------------------------------------------------
 Name: OS_Milli2Ticks

 Purpose: This function accepts a time interval in milliseconds as input an
 returns the tick equivalent is o.s. system clock ticks. The tick
 value is rounded up.  This algorthim should change to use a integer divide.
 ---------------------------------------------------------------------------------------*/

int32 OS_Milli2Ticks(uint32 milli_seconds) {
	uint32 num_of_ticks;
	uint32 tick_duration_usec;

	tick_duration_usec = OS_Tick2Micros();
	num_of_ticks = ((milli_seconds * 1000) + tick_duration_usec - 1)
					/ tick_duration_usec;

	return (num_of_ticks);
}/* end OS_Milli2Ticks */

/*---------------------------------------------------------------------------------------
 Name: OS_Tick2Micros

 Purpose: This function returns the duration of a system tick in micro seconds.
 ---------------------------------------------------------------------------------------*/

int32 OS_Tick2Micros(void) {
#if configTICK_RATE_HZ > 1000000
#error This does not work when the clock rate is greater than 1000000 hertz
#endif
	return (1000000 / configTICK_RATE_HZ);
}/* end OS_InfoGetTicks */

/*---------------------------------------------------------------------------------------
 * Name: OS_GetLocalTime
 * 
 * Purpose: This functions get the local time of the machine its on
 * ------------------------------------------------------------------------------------*/

int32 OS_GetLocalTime(OS_time_t *time_struct) {
	if (time_struct == NULL) {
		return OS_INVALID_POINTER;
	}

	time_struct->seconds = getElapsedSeconds() + adjust_seconds;
	time_struct->microsecs = getElapsedMicroseconds() + adjust_microseconds;
	while (time_struct->microsecs < 0) {
		time_struct->microsecs += 1000;
		time_struct->seconds--;
	}

	return OS_SUCCESS;
} /* end OS_GetLocalTime */

/*---------------------------------------------------------------------------------------
 * Name: OS_SetLocalTime
 * 
 * Purpose: This function sets the local time of the machine its on
 * ------------------------------------------------------------------------------------*/

int32 OS_SetLocalTime(OS_time_t *time_struct) {
	if (time_struct == NULL) {
		return OS_INVALID_POINTER;
	}

	adjust_seconds = time_struct->seconds - getElapsedSeconds();
	adjust_microseconds = time_struct->microsecs - getElapsedMicroseconds();
	while (adjust_microseconds < 0) {
		adjust_microseconds += 1000;
		adjust_seconds--;
	}

	return OS_SUCCESS;
} /* end OS_SetLocalTime */

/****************************************************************************************
 INT API
 ****************************************************************************************/

/*---------------------------------------------------------------------------------------
 Name: OS_IntAttachHandler

 Purpose: The call associates a specified C routine to a specified interrupt
 number.Upon occurring of the InterruptNumber the InerruptHandler
 routine will be called and passed the parameter.

 Parameters:
 InterruptNumber : The Interrupt Number that will cause the start of the ISR
 InerruptHandler : The ISR associatd with this interrupt
 parameter :The parameter that is passed to the ISR

 ---------------------------------------------------------------------------------------*/
int32 OS_IntAttachHandler(uint32 InterruptNumber,
		osal_task_entry InterruptHandler, int32 parameter) {
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_ERR_NOT_IMPLEMENTED;
}/* end OS_IntAttachHandler */
/*---------------------------------------------------------------------------------------
 Name: OS_IntUnlock

 Purpose: Enable previous state of interrupts

 Parameters:
 IntLevel : The Interrupt Level to be reinstated
 ---------------------------------------------------------------------------------------*/

int32 OS_IntUnlock(int32 IntLevel) {
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_SUCCESS;
}/* end OS_IntUnlock */

/*---------------------------------------------------------------------------------------
 Name: OS_IntLock

 Purpose: Disable interrupts.

 Parameters:

 Returns: Interrupt level
 ---------------------------------------------------------------------------------------*/

int32 OS_IntLock(void) {
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_SUCCESS;
}/* end OS_IntLock */

/*---------------------------------------------------------------------------------------
 Name: OS_IntEnable

 Purpose: Enable previous state of interrupts

 Parameters:
 IntLevel : The Interrupt Level to be reinstated
 ---------------------------------------------------------------------------------------*/

int32 OS_IntEnable(int32 Level) {
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_SUCCESS;
}/* end OS_IntEnable */

/*---------------------------------------------------------------------------------------
 Name: OS_IntDisable

 Purpose: Disable the corresponding interrupt number.

 Parameters:

 Returns: Interrupt level before OS_IntDisable Call
 ---------------------------------------------------------------------------------------*/

int32 OS_IntDisable(int32 Level) {
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_SUCCESS;
}/* end OS_IntDisable */

/*---------------------------------------------------------------------------------------
 Name: OS_HeapGetInfo

 Purpose: Return current info on the heap

 Parameters:

 ---------------------------------------------------------------------------------------*/
int32 OS_HeapGetInfo(OS_heap_prop_t *heap_prop) {
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_ERR_NOT_IMPLEMENTED;
}

/*---------------------------------------------------------------------------------------
 *  Name: OS_GetErrorName()
 *  purpose: A handy function to copy the name of the error to a buffer.
 ---------------------------------------------------------------------------------------*/
int32 OS_GetErrorName(int32 error_num, os_err_name_t * err_name) {
	/*
	 * Implementation note for developers:
	 *
	 * The size of the string literals below (including the terminating null)
	 * must fit into os_err_name_t.  Always check the string length when
	 * adding or modifying strings in this function.  If changing os_err_name_t
	 * then confirm these strings will fit.
	 */

	os_err_name_t local_name;
	uint32 return_code;

	return_code = OS_SUCCESS;

	switch (error_num) {
	case OS_SUCCESS:
		strcpy(local_name, "OS_SUCCESS");
		break;
	case OS_ERROR:
		strcpy(local_name, "OS_ERROR");
		break;
	case OS_INVALID_POINTER:
		strcpy(local_name, "OS_INVALID_POINTER");
		break;
	case OS_ERROR_ADDRESS_MISALIGNED:
		strcpy(local_name, "OS_ADDRESS_MISALIGNED");
		break;
	case OS_ERROR_TIMEOUT:
		strcpy(local_name, "OS_ERROR_TIMEOUT");
		break;
	case OS_INVALID_INT_NUM:
		strcpy(local_name, "OS_INVALID_INT_NUM");
		break;
	case OS_SEM_FAILURE:
		strcpy(local_name, "OS_SEM_FAILURE");
		break;
	case OS_SEM_TIMEOUT:
		strcpy(local_name, "OS_SEM_TIMEOUT");
		break;
	case OS_QUEUE_EMPTY:
		strcpy(local_name, "OS_QUEUE_EMPTY");
		break;
	case OS_QUEUE_FULL:
		strcpy(local_name, "OS_QUEUE_FULL");
		break;
	case OS_QUEUE_TIMEOUT:
		strcpy(local_name, "OS_QUEUE_TIMEOUT");
		break;
	case OS_QUEUE_INVALID_SIZE:
		strcpy(local_name, "OS_QUEUE_INVALID_SIZE");
		break;
	case OS_QUEUE_ID_ERROR:
		strcpy(local_name, "OS_QUEUE_ID_ERROR");
		break;
	case OS_ERR_NAME_TOO_LONG:
		strcpy(local_name, "OS_ERR_NAME_TOO_LONG");
		break;
	case OS_ERR_NO_FREE_IDS:
		strcpy(local_name, "OS_ERR_NO_FREE_IDS");
		break;
	case OS_ERR_NAME_TAKEN:
		strcpy(local_name, "OS_ERR_NAME_TAKEN");
		break;
	case OS_ERR_INVALID_ID:
		strcpy(local_name, "OS_ERR_INVALID_ID");
		break;
	case OS_ERR_NAME_NOT_FOUND:
		strcpy(local_name, "OS_ERR_NAME_NOT_FOUND");
		break;
	case OS_ERR_SEM_NOT_FULL:
		strcpy(local_name, "OS_ERR_SEM_NOT_FULL");
		break;
	case OS_ERR_INVALID_PRIORITY:
		strcpy(local_name, "OS_ERR_INVALID_PRIORITY");
		break;

	default:
		strcpy(local_name, "ERROR_UNKNOWN");
		return_code = OS_ERROR;
	}

	strcpy((char*) err_name, local_name);

	return return_code;
}
/*---------------------------------------------------------------------------------------
 * Name: OS_FindCreator
 * Purpose: Finds the creator of a the current task  to store in the table for lookup 
 *          later 
 ---------------------------------------------------------------------------------------*/

uint32 OS_FindCreator(void) {
	TaskHandle_t task_id;
	int i;
	/* find the calling task ID */
	task_id = xTaskGetCurrentTaskHandle();

	for (i = 0; i < OS_TASK_TABLE_SIZE; i++) {
		if (task_id == OS_task_table[i].id)
			break;
	}
	return i;
}
/* ---------------------------------------------------------------------------
 * Name: OS_printf 
 * 
 * Purpose: This function abstracts out the printf type statements. This is 
 *          useful for using OS- specific thats that will allow non-polled
 *          print statements for the real time systems. 
 *
 ---------------------------------------------------------------------------*/
void OS_printf(const char *String, ...) {
	va_list ptr;
	char msg_buffer[OS_BUFFER_SIZE];

	/* MUST NOT BE CALLED FROM AN ISR */
	/* MUST NOT BE CALLED FROM AN ISR */
	/* MUST NOT BE CALLED FROM AN ISR */
	/* MUST NOT BE CALLED FROM AN ISR */
	/* MUST NOT BE CALLED FROM AN ISR */
	/* MUST NOT BE CALLED FROM AN ISR */
	/* MUST NOT BE CALLED FROM AN ISR */
	/* MUST NOT BE CALLED FROM AN ISR */

	if (OS_printf_enabled == TRUE) {
		va_start(ptr, String);
		vsnprintf(&msg_buffer[0], (size_t) OS_BUFFER_SIZE, String, ptr);
		va_end(ptr);

		msg_buffer[OS_BUFFER_SIZE - 1] = '\0';
		printf("%s", &msg_buffer[0]);
		fflush(stdout);
	}
}/* end OS_printf*/

/* ---------------------------------------------------------------------------
 * Name: OS_printf_disable
 * 
 * Purpose: This function disables the output to the UART from OS_printf.  
 *
 ---------------------------------------------------------------------------*/
void OS_printf_disable(void) {
	OS_printf_enabled = FALSE;
}/* end OS_printf_disable*/

/* ---------------------------------------------------------------------------
 * Name: OS_printf_enable
 * 
 * Purpose: This function enables the output to the UART through OS_printf.  
 *
 ---------------------------------------------------------------------------*/
void OS_printf_enable(void) {
	OS_printf_enabled = TRUE;
}/* end OS_printf_enable*/

/*
 **
 **   Name: OS_FPUExcSetMask
 **
 **   Purpose: This function sets the FPU exception mask
 **
 **   Notes: The exception environment is local to each task Therefore this must be
 **          called for each task that that wants to do floating point and catch exceptions.
 */
int32 OS_FPUExcSetMask(uint32 mask) {
	/*
	 ** Not implemented in FreeRTOS.
	 */
	return (OS_SUCCESS);
}

/*
 **
 **   Name: OS_FPUExcGetMask
 **
 **   Purpose: This function gets the FPU exception mask
 **
 **   Notes: The exception environment is local to each task Therefore this must be
 **          called for each task that that wants to do floating point and catch exceptions.
 */
int32 OS_FPUExcGetMask(uint32 *mask) {
	/*
	 ** Not implemented in FreeRTOS.
	 */
	return (OS_SUCCESS);
}
