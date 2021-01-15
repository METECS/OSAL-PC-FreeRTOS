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
 * \file   ostimer.c
 * \author Christopher Sullivan based on work by joseph.p.hickey@nasa.gov and Jonathan Brandenburg
 *
 * Purpose: This file contains the OSAL Timer API for FreeRTOS
 */

/****************************************************************************************
 INCLUDE FILES
 ***************************************************************************************/

#include "os-FreeRTOS.h"
#include "timers.h"

/****************************************************************************************
 EXTERNAL FUNCTION PROTOTYPES
 ***************************************************************************************/

TickType_t getElapsedSeconds();
TickType_t getElapsedMicroseconds();

/****************************************************************************************
 INTERNAL FUNCTION PROTOTYPES
 ****************************************************************************************/

int32 OS_TimerGetIdByHostId(uint32 *timer_id, TimerHandle_t host_timer_id);

/****************************************************************************************
 DEFINES
 ****************************************************************************************/

/* Each "timebase" resource spawns an dedicated servicing task-
 * this task (not the timer ISR) is the context that calls back to
 * the user application.
 *
 * This should run at the highest priority to reduce latency.
 */
#define OSAL_TIMEBASE_TASK_STACK_SIZE       configTIMER_TASK_STACK_DEPTH
#define OSAL_TIMEBASE_TASK_PRIORITY         configTIMER_TASK_PRIORITY

/****************************************************************************************
 LOCAL TYPEDEFS
 ****************************************************************************************/

typedef struct
{
	TimerHandle_t				host_timer_id;
	SemaphoreHandle_t           tick_sem;
	SemaphoreHandle_t           handler_mutex;
	TaskHandle_t         		handler_task;
    uint8						simulate_flag;
    uint8		        	    reset_flag;
    TickType_t					interval_ticks;
} OS_impl_timebase_internal_record_t;

/****************************************************************************************
 GLOBAL DATA
 ****************************************************************************************/

OS_impl_timebase_internal_record_t OS_impl_timebase_table[OS_MAX_TIMEBASES];

static int32 adjust_seconds = 0;
static int32 adjust_microseconds = 0;

/*----------------------------------------------------------------
 *
 * Function: OS_TimeBaseLock_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
void OS_TimeBaseLock_Impl(uint32 local_id)
{
	xSemaphoreTake(OS_impl_timebase_table[local_id].handler_mutex, portMAX_DELAY);
} /* end OS_TimeBaseLock_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TimeBaseUnlock_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
void OS_TimeBaseUnlock_Impl(uint32 local_id)
{
	xSemaphoreGive(OS_impl_timebase_table[local_id].handler_mutex);
} /* end OS_TimeBaseUnlock_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_Callback
 *
 *  Purpose: Local helper routine, not part of OSAL API.
 *
 *-----------------------------------------------------------------*/
void OS_Callback(TimerHandle_t xTimer)
{
	BaseType_t status;
	/* Find the id of the timer */
	uint32 local_id;
	OS_impl_timebase_internal_record_t *local;
	if(OS_TimerGetIdByHostId(&local_id, xTimer) == OS_SUCCESS)
	{
		local = &OS_impl_timebase_table[local_id];

		/*
		 * Reset the timer, but only if an interval was selected
		 */
		if(local->interval_ticks > 0)
		{
			status = xTimerChangePeriod(local->host_timer_id, local->interval_ticks, portMAX_DELAY);
			if(status == pdPASS)
			{
				xTimerStart(local->host_timer_id, portMAX_DELAY);
			}
		}

		/*
		 * FreeRTOS OS timers implemented with an ISR callback
		 * this must be downgraded to an ordinary task context
		 *
		 * This is accomplished by just releasing a semaphore here.
		 */
		xSemaphoreGive(local->tick_sem);
	}

} /* end OS_Callback */

/*----------------------------------------------------------------
 *
 * Function: OS_TimeBase_WaitImpl
 *
 *  Purpose: Local helper routine, not part of OSAL API.
 *           Pends on the semaphore for the next timer tick
 *
 *-----------------------------------------------------------------*/
static uint32 OS_TimeBase_WaitImpl(uint32 local_id)
{
    OS_impl_timebase_internal_record_t *local;
    uint32 interval_time;

    local = &OS_impl_timebase_table[local_id];

    /*
     * Determine how long this tick was.
     * Note that there are plenty of ways this become wrong if the timer
     * is reset right around the time a tick comes in.  However, it is
     * impossible to guarantee the behavior of a reset if the timer is running.
     * (This is not an expected use-case anyway; the timer should be set and forget)
     */
    if(local->reset_flag == 0)
    {
        interval_time = OS_timebase_table[local_id].nominal_interval_time;
    }
    else
    {
        interval_time = OS_timebase_table[local_id].nominal_start_time;
        local->reset_flag = 0;
    }

    /*
     * Pend for the tick arrival
     */
    xSemaphoreTake(local->tick_sem, portMAX_DELAY);

    return interval_time;
} /* end OS_TimeBase_WaitImpl */

/****************************************************************************************
 INITIALIZATION FUNCTION
 ***************************************************************************************/

/*----------------------------------------------------------------
 *
 *  Function:  OS_FreeRTOS_TimeBaseAPI_Impl_Init
 *
 *  Purpose:  Initialize the timer implementation layer
 *
 *-----------------------------------------------------------------*/
int32 OS_FreeRTOS_TimeBaseAPI_Impl_Init(void)
{
	TickType_t ticks_per_sec = configTICK_RATE_HZ;

	if(ticks_per_sec <= 0)
	{
		return OS_ERROR;
	}

	/*
	* For the global ticks per second, use the value direct from the config
	*/
	OS_SharedGlobalVars.TicksPerSecond = (int32)ticks_per_sec;

	/*
	* Compute the clock accuracy in Nanoseconds (ns per tick)
	* This really should be an exact/whole number result; otherwise this
	* will round to the nearest nanosecond.
	*/
	FreeRTOS_GlobalVars.ClockAccuracyNsec = (1000000000 + (OS_SharedGlobalVars.TicksPerSecond / 2)) / OS_SharedGlobalVars.TicksPerSecond;

	/*
	* Finally compute the Microseconds per tick that is used for OS_Tick2Micros() call
	* This must further round again to the nearest microsecond, so it is undesirable to use
	* this for time computations if the result is not exact.
	*/
	OS_SharedGlobalVars.MicroSecPerTick = (FreeRTOS_GlobalVars.ClockAccuracyNsec + 500) / 1000;

	return OS_SUCCESS;
} /* end OS_FreeRTOS_TimeBaseAPI_Impl_Init */

/****************************************************************************************
 INTERNAL FUNCTIONS
 ****************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_UsecsToTicks
 *
 *  Purpose:  Convert Microseconds to a number of ticks.
 *
 *-----------------------------------------------------------------*/
void  OS_UsecsToTicks(uint32 usecs, TickType_t *ticks)
{
   uint32 result;

   /*
    * In order to compute without overflowing a 32 bit integer,
    * this is done in 2 parts -
    * the fractional seconds first then add any whole seconds.
    * the fractions are rounded UP so that this is guaranteed to produce
    * a nonzero number of ticks for a nonzero number of microseconds.
    */

   result = (1000 * (usecs % 1000000) + FreeRTOS_GlobalVars.ClockAccuracyNsec - 1) / FreeRTOS_GlobalVars.ClockAccuracyNsec;

   if(usecs >= 1000000)
   {
      result += (usecs / 1000000) * OS_SharedGlobalVars.TicksPerSecond;
   }

   *ticks = (TickType_t)result;
} /* end OS_UsecsToTicks */

/*----------------------------------------------------------------
 *
 * Function: OS_TimerGetIdByHostId
 *
 *  Purpose:  This function tries to find a Timer Id given the host-specific id
 *             The id is returned through timer_id
 *
 *-----------------------------------------------------------------*/
int32 OS_TimerGetIdByHostId(uint32 *timer_id, TimerHandle_t host_timer_id)
{
	uint32 i;

	if(timer_id == NULL || host_timer_id == NULL)
	{
		return OS_INVALID_POINTER;
	}

	for(i = 0; i < OS_MAX_TIMEBASES; i++)
	{
		if(OS_impl_timebase_table[i].host_timer_id == host_timer_id)
		{
			*timer_id = i;
			return OS_SUCCESS;
		}
	}

	/*
	 ** The name was not found in the table
	 */
	return OS_ERR_NAME_NOT_FOUND;
}/* end OS_TimerGetIdByName */

/****************************************************************************************
 Time Base API
 ***************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_TimeBaseCreate_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TimeBaseCreate_Impl(uint32 timer_id)
{
	int32 return_code;
	OS_impl_timebase_internal_record_t *local;
	OS_common_record_t *global;
	char * dummy = "";

    return_code = OS_SUCCESS;
    local = &OS_impl_timebase_table[timer_id];
    global = &OS_global_timebase_table[timer_id];

    /*
	 * Set up the necessary OS constructs
	 *
	 * If an external sync function is used then there is nothing to do here -
	 * we simply call that function and it should synchronize to the time source.
	 *
	 * If no external sync function is provided then this will set up an FreeRTOS
	 * timer to locally simulate the timer tick using the CPU clock.
	 */
	local->simulate_flag = (OS_timebase_table[timer_id].external_sync == NULL);
	if(local->simulate_flag)
	{
		OS_timebase_table[timer_id].external_sync = OS_TimeBase_WaitImpl;

		/*
		 * The tick_sem is a simple semaphore posted by the ISR and taken by the
		 * timebase helper task (created later).
		 */
		local->tick_sem = xSemaphoreCreateBinary();

		if(local->tick_sem == NULL)
		{
			return_code = OS_TIMER_ERR_INTERNAL;
		}

		/*
		 * The handler_mutex is deals with access to the callback list for this timebase
		 */
		local->handler_mutex = xSemaphoreCreateMutex();
		if(local->handler_mutex == NULL)
		{
			vSemaphoreDelete(local->tick_sem);
			return_code = OS_TIMER_ERR_INTERNAL;
		}

		//Set interval_ticks to 1 to start
		local->interval_ticks = 1;
		/*
		 ** Create an interval timer
		 */
		local->host_timer_id = xTimerCreate(dummy,
									local->interval_ticks,
									pdFALSE,
									0,
									OS_Callback);

		if(local->host_timer_id == NULL)
		{
			vSemaphoreDelete (local->handler_mutex);
			vSemaphoreDelete (local->tick_sem);
			return_code = OS_TIMER_ERR_UNAVAILABLE;
		}
	}

	/*
	 * Spawn a dedicated time base handler thread
	 *
	 * This alleviates the need to handle expiration in the context of a signal handler -
	 * The handler thread can call a BSP synchronized delay implementation as well as the
	 * application callback function.  It should run with elevated priority to reduce latency.
	 *
	 * Note the thread will not actually start running until this function exits and releases
	 * the global table lock.
	 */
	if(return_code == OS_SUCCESS)
	{
		BaseType_t status;
		status = xTaskCreate((TaskFunction_t) OS_TimeBase_CallbackThread,
							dummy,
							OSAL_TIMEBASE_TASK_STACK_SIZE,
							(void *)global->active_id,
							OSAL_TIMEBASE_TASK_PRIORITY,
							&local->handler_task);

		if(status != pdPASS)
		{
			return_code = OS_TIMER_ERR_INTERNAL;
			/* Also delete the resources we allocated earlier */
			xTimerDelete(local->host_timer_id, portMAX_DELAY);
			vSemaphoreDelete(local->handler_mutex);
			vSemaphoreDelete (local->tick_sem);
		}
	}

	return return_code;
} /* end OS_TimeBaseCreate_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TimeBaseSet_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TimeBaseSet_Impl(uint32 timer_id, int32 start_time, int32 interval_time)
{
	OS_impl_timebase_internal_record_t *local;
	int32 return_code;
	BaseType_t status;
    TickType_t start_ticks;

	local = &OS_impl_timebase_table[timer_id];
	return_code = OS_SUCCESS;

	/* There is only something to do here if we are generating a simulated tick */
	if(local->simulate_flag)
	{
		/*
		** Note that UsecsToTicks() already protects against intervals
		** less than os_clock_accuracy -- no need for extra checks which
		** would actually possibly make it less accurate.
		**
		** Still want to preserve zero, since that has a special meaning.
		*/

		if(start_time <= 0)
		{
			interval_time = 0;  /* cannot have interval without start */
		}

		if(interval_time <= 0)
		{
			local->interval_ticks = 0;
		}
		else
		{
			OS_UsecsToTicks(interval_time, &local->interval_ticks);
		}

		if(start_time > 0)
		{
			/*
			** Convert from Microseconds to the timeout
			*/
			OS_UsecsToTicks(start_time, &start_ticks);

			status = xTimerChangePeriod(local->host_timer_id, start_ticks, portMAX_DELAY);
			if(status == pdPASS)
			{
				status = xTimerStart(local->host_timer_id, portMAX_DELAY);
				if(status == pdPASS)
				{
					if(local->interval_ticks > 0)
					{
					   start_ticks = local->interval_ticks;
					}

					OS_timebase_table[timer_id].accuracy_usec = (start_ticks * 100000) / OS_SharedGlobalVars.TicksPerSecond;
					OS_timebase_table[timer_id].accuracy_usec *= 10;
				}
				else
				{
					return_code = OS_TIMER_ERR_INTERNAL;
				}
			}
			else
			{
				return_code = OS_TIMER_ERR_INTERNAL;
			}
		}
	}

	if(local->reset_flag == 0 && return_code == OS_SUCCESS)
	{
		local->reset_flag = 1;
	}

	return return_code;
} /* end OS_TimeBaseSet_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TimeBaseDelete_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TimeBaseDelete_Impl(uint32 timer_id)
{
	OS_impl_timebase_internal_record_t *local;
	BaseType_t status;
	int32 return_code;

	local = &OS_impl_timebase_table[timer_id];
	return_code = OS_SUCCESS;

	/*
	** Delete the tasks and timer OS constructs first, then delete the
	** semaphores.  If the task/timer is running it might try to use them.
	*/
	if(local->simulate_flag)
	{
		status = xTimerDelete(local->host_timer_id, portMAX_DELAY);
		if(status != pdPASS)
		{
			return OS_TIMER_ERR_INTERNAL;
		}

	}

	vTaskDelete(local->handler_task);

	/*
	 * If any delete/cleanup calls fail, unfortunately there is no recourse.
	 */
	if(return_code == OS_SUCCESS)
	{
		vSemaphoreDelete(local->handler_mutex);

		if(local->simulate_flag)
		{
			vSemaphoreDelete(local->tick_sem);
            local->simulate_flag = 0;
		}
	}

	return return_code;
} /* end OS_TimeBaseDelete_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TimeBaseGetInfo_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TimeBaseGetInfo_Impl(uint32 timer_id, OS_timebase_prop_t *timer_prop)
{
    return OS_SUCCESS;
} /* end OS_TimeBaseGetInfo_Impl */

/****************************************************************************************
 Other Time-Related API Implementation
 ***************************************************************************************/

/*---------------------------------------------------------------------------------------
 * Name: OS_GetLocalTime_Impl
 *
 * Purpose: This functions get the local time of the machine its on
 * ------------------------------------------------------------------------------------*/
int32 OS_GetLocalTime_Impl(OS_time_t *time_struct)
{
	time_struct->seconds = getElapsedSeconds() + adjust_seconds;
	time_struct->microsecs = getElapsedMicroseconds() + adjust_microseconds;
	while(time_struct->microsecs < 0)
	{
		time_struct->microsecs += 1000;
		time_struct->seconds--;
	}

	return OS_SUCCESS;
} /* end OS_GetLocalTime_Impl */

/*---------------------------------------------------------------------------------------
 * Name: OS_SetLocalTime_Impl
 *
 * Purpose: This function sets the local time of the machine its on
 * ------------------------------------------------------------------------------------*/
int32 OS_SetLocalTime_Impl(const OS_time_t *time_struct)
{
	adjust_seconds = time_struct->seconds - getElapsedSeconds();
	adjust_microseconds = time_struct->microsecs - getElapsedMicroseconds();
	while(adjust_microseconds < 0)
	{
		adjust_microseconds += 1000;
		adjust_seconds--;
	}

	return OS_SUCCESS;
} /* end OS_SetLocalTime_Impl */
