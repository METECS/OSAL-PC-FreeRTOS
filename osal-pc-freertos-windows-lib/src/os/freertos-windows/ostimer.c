/*
 * This file contains the OSAL Timer API for FreeRTOS
 *
 * Based on src/os/rtems/ostimer.c from the OSAL distribution wit the
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
#include "timers.h"
#include "semphr.h"

#include "common_types.h"
#include "osapi.h"
#include "ostimerex.h"

/****************************************************************************************
 EXTERNAL FUNCTION PROTOTYPES
 ****************************************************************************************/

uint32 OS_FindCreator(void);

/****************************************************************************************
 INTERNAL FUNCTION PROTOTYPES
 ****************************************************************************************/

void OS_TicksToUsecs(TickType_t ticks, uint32 *usecs);
void OS_TimerShim(TimerHandle_t xTimer);

/****************************************************************************************
 DEFINES
 ****************************************************************************************/

#define UNINITIALIZED 0

/****************************************************************************************
 LOCAL TYPEDEFS
 ****************************************************************************************/

typedef struct {
	uint32 free;
	char name[OS_MAX_API_NAME];
	char original_name[MAX_API_NAME_INCOMING];
	uint32 creator;
	uint32 start_time;
	uint32 interval_time;
	uint32 accuracy;
	OS_TimerCallback_t callback_ptr;
	TimerHandle_t host_timerid;
	uint32 entry_number; /* This is the entry number of each instance of this structure in the queue table */

} OS_timer_internal_record_t;

/****************************************************************************************
 GLOBAL DATA
 ****************************************************************************************/

OS_timer_internal_record_t OS_timer_table[OS_MAX_TIMERS];
uint32 os_clock_accuracy;

/*
 ** The Mutex for protecting the above table
 */
SemaphoreHandle_t OS_timer_table_sem;

/* A counter to ensure generated names are unique */
static uint8 name_counter = 0;

/****************************************************************************************
 INITIALIZATION FUNCTION
 ****************************************************************************************/
int32 OS_TimerAPIInit(void) {
	int i;
	int32 return_code = OS_SUCCESS;

	/*
	 ** Mark all timers as available
	 */
	for (i = 0; i < OS_MAX_TIMERS; i++) {
		OS_timer_table[i].free = TRUE;
		OS_timer_table[i].creator = UNINITIALIZED;
		strcpy(OS_timer_table[i].name, "");
		OS_timer_table[i].entry_number = i;
	}

	/*
	 ** Store the clock accuracy for 1 tick.
	 */
	OS_TicksToUsecs(1, &os_clock_accuracy);

	/*
	 ** Create the Timer Table semaphore
	 */
	OS_timer_table_sem = xSemaphoreCreateMutex();
	if (OS_timer_table_sem == NULL) {
		return_code = OS_ERROR;
		return (return_code);
	}

	return (return_code);

}

/****************************************************************************************
 INTERNAL FUNCTIONS
 ****************************************************************************************/

/******************************************************************************
 **  Function:  OS_UsecToTicks
 **
 **  Purpose:  Convert Microseconds to a number of ticks.
 **
 */
void OS_UsecsToTicks(uint32 usecs, TickType_t *ticks) {
	TickType_t ticks_per_sec = configTICK_RATE_HZ;
	uint32 usecs_per_tick;

	usecs_per_tick = (1000000) / ticks_per_sec;

	if (usecs < usecs_per_tick) {
		*ticks = 1;
	} else {
		*ticks = usecs / usecs_per_tick;
		/* Need to round up?? */
	}
}

/******************************************************************************
 **  Function:  OS_TicksToUsec
 **
 **  Purpose:  Convert a number of Ticks to microseconds
 **
 */
void OS_TicksToUsecs(TickType_t ticks, uint32 *usecs) {
	TickType_t ticks_per_sec = configTICK_RATE_HZ;
	uint32 usecs_per_tick;

	usecs_per_tick = (1000000) / ticks_per_sec;

	*usecs = ticks * usecs_per_tick;
}

/****************************************************************************************
 Timer API
 ****************************************************************************************/

/******************************************************************************
 **  Function:  OS_TimerCreate
 **
 **  Purpose:  Create a new OSAL Timer
 **
 **  Arguments:
 **
 **  Return:
 */
int32 OS_TimerCreate(uint32 *timer_id, const char *timer_name,
		uint32 *clock_accuracy, OS_TimerCallback_t callback_ptr) {
	uint32 possible_tid;
	int32 i;

	if (timer_id == NULL || timer_name == NULL || clock_accuracy == NULL) {
		return OS_INVALID_POINTER;
	}

	char os_unique_name[OS_MAX_API_NAME];
	snprintf(os_unique_name, OS_MAX_API_NAME, "%-*.*s%02x", (OS_MAX_API_NAME-3), (OS_MAX_API_NAME-3), timer_name, name_counter++);

	/*
	 ** we don't want to allow names too long
	 ** if truncated, two names might be the same
	 */
	if (strlen(timer_name) > MAX_API_NAME_INCOMING) {
		return OS_ERR_NAME_TOO_LONG;
	}
	if (strlen(os_unique_name) >= OS_MAX_API_NAME) {
		return OS_ERR_NAME_TOO_LONG;
	}

	/*
	 ** Check Parameters
	 */
	xSemaphoreTake(OS_timer_table_sem, portMAX_DELAY);

	for (possible_tid = 0; possible_tid < OS_MAX_TIMERS; possible_tid++) {
		if (OS_timer_table[possible_tid].free == TRUE)
			break;
	}

	if (possible_tid >= OS_MAX_TIMERS
			|| OS_timer_table[possible_tid].free != TRUE) {
		xSemaphoreGive(OS_timer_table_sem);
		return OS_ERR_NO_FREE_IDS;
	}

	/*
	 ** Check to see if the name is already taken
	 */
	for (i = 0; i < OS_MAX_TIMERS; i++) {
		if ((OS_timer_table[i].free == FALSE)
				&& strcmp((char*) timer_name, OS_timer_table[i].original_name) == 0) {
			xSemaphoreGive(OS_timer_table_sem);
			return OS_ERR_NAME_TAKEN;
		}
	}

	/*
	 ** Verify callback parameter
	 */
	if (callback_ptr == NULL) {
		xSemaphoreGive(OS_timer_table_sem);
		return OS_TIMER_ERR_INVALID_ARGS;
	}

	/*
	 ** Set the possible timer Id to not free so that
	 ** no other task can try to use it
	 */
	OS_timer_table[possible_tid].free = FALSE;
	xSemaphoreGive(OS_timer_table_sem);

	OS_timer_table[possible_tid].creator = OS_FindCreator();
	strncpy(OS_timer_table[possible_tid].name, os_unique_name, OS_MAX_API_NAME);
	strncpy(OS_timer_table[possible_tid].original_name, timer_name, MAX_API_NAME_INCOMING);
	OS_timer_table[possible_tid].start_time = 0;
	OS_timer_table[possible_tid].interval_time = 1;
	OS_timer_table[possible_tid].callback_ptr = callback_ptr;

	/*
	 ** Create an interval timer
	 */
	OS_timer_table[possible_tid].host_timerid = xTimerCreate(
			OS_timer_table[possible_tid].name,
			OS_timer_table[possible_tid].interval_time,
			pdFALSE, &OS_timer_table[i].entry_number, OS_TimerShim);
	if (OS_timer_table[possible_tid].host_timerid == NULL) {
		OS_timer_table[possible_tid].free = TRUE;
		return (OS_TIMER_ERR_UNAVAILABLE);
	}

	/*
	 ** Return the clock accuracy to the user
	 */
	*clock_accuracy = os_clock_accuracy;

	/*
	 ** Return timer ID
	 */
	*timer_id = possible_tid;

	return OS_SUCCESS;
}

/******************************************************************************
 **  Function:  OS_TimerShim
 **
 **  Purpose: This is the function called when a timer trigers. It will call
 **  the callback function and then rearm the timer if the interval is > 0
 **
 **  Arguments:
 **    xTimer - The OS-specific id of the timer
 **
 **  Return:
 **    (none)
 */
void OS_TimerShim(TimerHandle_t xTimer) {
	TickType_t timeout;
	BaseType_t status;

	/* Find the id of the timer */
	uint32 timer_id;
	if (OS_TimerGetIdByHostId(&timer_id, xTimer) == OS_SUCCESS) {
		/* Ensure the timer id is valid */
		if (timer_id < OS_MAX_TIMERS || OS_timer_table[timer_id].free != TRUE) {
			/* perform the callback */
			OS_timer_table[timer_id].callback_ptr(timer_id);

			/* Rearm the timer */
			if (OS_timer_table[timer_id].interval_time > 0) {
				/*
				 ** Convert from Microseconds to the timeout
				 */
				OS_UsecsToTicks(OS_timer_table[timer_id].interval_time,
						&timeout);

				status = xTimerChangePeriod(
						OS_timer_table[timer_id].host_timerid, timeout,
						portMAX_DELAY);
				if (status == pdPASS) {
					xTimerStart(OS_timer_table[timer_id].host_timerid,
							portMAX_DELAY);
				}
			}
		}
	}

}

/******************************************************************************
 **  Function:  OS_TimerSet
 **
 **  Purpose:
 **
 **  Arguments:
 **    (none)
 **
 **  Return:
 **    (none)
 */
int32 OS_TimerSet(uint32 timer_id, uint32 start_time, uint32 interval_time) {
	TickType_t timeout;
	BaseType_t status;

	/*
	 ** Check to see if the timer_id given is valid
	 */
	if (timer_id >= OS_MAX_TIMERS || OS_timer_table[timer_id].free == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	/*
	 ** Round up the accuracy of the start time and interval times.
	 ** Still want to preserve zero, since that has a special meaning.
	 */
	if ((start_time > 0) && (start_time < os_clock_accuracy)) {
		start_time = os_clock_accuracy;
	}

	if ((interval_time > 0) && (interval_time < os_clock_accuracy)) {
		interval_time = os_clock_accuracy;
	}

	/*
	 ** Save the start and interval times
	 */
	OS_timer_table[timer_id].start_time = start_time;
	OS_timer_table[timer_id].interval_time = interval_time;

	/*
	 ** The defined behavior is to not arm the timer if the start time is zero
	 ** If the interval time is zero, then the timer will not be re-armed.
	 */
	if (start_time > 0) {
		/*
		 ** Convert from Microseconds to the timeout
		 */
		OS_UsecsToTicks(start_time, &timeout);

		status = xTimerChangePeriod(OS_timer_table[timer_id].host_timerid,
				timeout, portMAX_DELAY);
		if (status != pdPASS) {
			return ( OS_TIMER_ERR_INTERNAL);
		}

		status = xTimerStart(OS_timer_table[timer_id].host_timerid,
				portMAX_DELAY);
		if (status != pdPASS) {
			return ( OS_TIMER_ERR_INTERNAL);
		}
	}
	return OS_SUCCESS;
}

/******************************************************************************
 **  Function:  OS_TimerDelete
 **
 **  Purpose:
 **
 **  Arguments:
 **    (none)
 **
 **  Return:
 **    (none)
 */
int32 OS_TimerDelete(uint32 timer_id) {
	BaseType_t status;

	/*
	 ** Check to see if the timer_id given is valid
	 */
	if (timer_id >= OS_MAX_TIMERS || OS_timer_table[timer_id].free == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	xSemaphoreTake(OS_timer_table_sem, portMAX_DELAY);
	OS_timer_table[timer_id].free = TRUE;
	xSemaphoreGive(OS_timer_table_sem);

	/*
	 ** Delete the timer
	 */
	status = xTimerDelete(OS_timer_table[timer_id].host_timerid, portMAX_DELAY);
	if (status != pdPASS) {
		return ( OS_TIMER_ERR_INTERNAL);
	}

	return OS_SUCCESS;
}

/***********************************************************************************
 **
 **    Name: OS_TimerGetIdByHostId
 **
 **    Purpose: This function tries to find a Timer Id given the host-specific id
 **             The id is returned through timer_id
 **
 **    Returns: OS_INVALID_POINTER if timer_id or timer_name are NULL pointers
 **             OS_ERR_NAME_NOT_FOUND if the name was not found in the table
 **             OS_SUCCESS if success
 **
 */
int32 OS_TimerGetIdByHostId(uint32 *timer_id, TimerHandle_t host_timer_id) {
	uint32 i;

	if (timer_id == NULL || host_timer_id == NULL) {
		return OS_INVALID_POINTER;
	}

	for (i = 0; i < OS_MAX_TIMERS; i++) {
		if (OS_timer_table[i].free != TRUE
				&& OS_timer_table[i].host_timerid == host_timer_id) {
			*timer_id = i;
			return OS_SUCCESS;
		}
	}

	/*
	 ** The name was not found in the table,
	 **  or it was, and the sem_id isn't valid anymore
	 */
	return OS_ERR_NAME_NOT_FOUND;

}/* end OS_TimerGetIdByName */

/***********************************************************************************
 **
 **    Name: OS_TimerGetIdByName
 **
 **    Purpose: This function tries to find a Timer Id given the name
 **             The id is returned through timer_id
 **
 **    Returns: OS_INVALID_POINTER if timer_id or timer_name are NULL pointers
 **             OS_ERR_NAME_TOO_LONG if the name given is to long to have been stored
 **             OS_ERR_NAME_NOT_FOUND if the name was not found in the table
 **             OS_SUCCESS if success
 **
 */
int32 OS_TimerGetIdByName(uint32 *timer_id, const char *timer_name) {
	uint32 i;

	if (timer_id == NULL || timer_name == NULL) {
		return OS_INVALID_POINTER;
	}

	/*
	 ** a name too long wouldn't have been allowed in the first place
	 ** so we definitely won't find a name too long
	 */
	if (strlen(timer_name) > MAX_API_NAME_INCOMING) {
		return OS_ERR_NAME_TOO_LONG;
	}

	for (i = 0; i < OS_MAX_TIMERS; i++) {
		if (OS_timer_table[i].free != TRUE
				&& (strcmp(OS_timer_table[i].original_name, (char*) timer_name) == 0)) {
			*timer_id = i;
			return OS_SUCCESS;
		}
	}

	/*
	 ** The name was not found in the table,
	 **  or it was, and the sem_id isn't valid anymore
	 */
	return OS_ERR_NAME_NOT_FOUND;

}/* end OS_TimerGetIdByName */

/***************************************************************************************
 **    Name: OS_TimerGetInfo
 **
 **    Purpose: This function will pass back a pointer to structure that contains
 **             all of the relevant info( name and creator) about the specified timer.
 **
 **    Returns: OS_ERR_INVALID_ID if the id passed in is not a valid timer
 **             OS_INVALID_POINTER if the timer_prop pointer is null
 **             OS_SUCCESS if success
 */
int32 OS_TimerGetInfo(uint32 timer_id, OS_timer_prop_t *timer_prop) {
	/*
	 ** Check to see that the id given is valid
	 */
	if (timer_id >= OS_MAX_TIMERS || OS_timer_table[timer_id].free == TRUE) {
		return OS_ERR_INVALID_ID;
	}

	if (timer_prop == NULL) {
		return OS_INVALID_POINTER;
	}

	/*
	 ** put the info into the stucture
	 */
	xSemaphoreTake(OS_timer_table_sem, portMAX_DELAY);

	timer_prop->creator = OS_timer_table[timer_id].creator;
	strcpy(timer_prop->name, OS_timer_table[timer_id].name);
	timer_prop->start_time = OS_timer_table[timer_id].start_time;
	timer_prop->interval_time = OS_timer_table[timer_id].interval_time;
	timer_prop->accuracy = OS_timer_table[timer_id].accuracy;

	xSemaphoreGive(OS_timer_table_sem);

	return OS_SUCCESS;
} /* end OS_TimerGetInfo */

/****************************************************************
 * TIME BASE API
 *
 * This is not implemented by this OSAL, so return "OS_ERR_NOT_IMPLEMENTED"
 * for all calls defined by this API.  This is necessary for forward
 * compatibility (runtime code can check for this return code and use
 * an alternative API where appropriate).
 */

int32 OS_TimeBaseCreate(uint32 *timer_id, const char *timebase_name,
		OS_TimerSync_t external_sync) {
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_ERR_NOT_IMPLEMENTED;
}

int32 OS_TimeBaseSet(uint32 timer_id, uint32 start_time, uint32 interval_time) {
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_ERR_NOT_IMPLEMENTED;
}

int32 OS_TimeBaseDelete(uint32 timer_id) {
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_ERR_NOT_IMPLEMENTED;
}

int32 OS_TimeBaseGetIdByName(uint32 *timer_id, const char *timebase_name) {
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_ERR_NOT_IMPLEMENTED;
}

int32 OS_TimerAdd(uint32 *timer_id, const char *timer_name, uint32 timebase_id,
		OS_ArgCallback_t callback_ptr, void *callback_arg) {
#warning OS_ERR_NOT_IMPLEMENTED: This function must be implemented on the specific hardware platform
	return OS_ERR_NOT_IMPLEMENTED;
}

