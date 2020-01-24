/*
 * ostimerex.h
 *
 *  Created on: Jan 30, 2019
 *      Author: Jonathan Brandenburg
 */

#ifndef OSAL_CORE_SRC_OS_FREERTOS_WINDOWS_OSTIMEREX_H_
#define OSAL_CORE_SRC_OS_FREERTOS_WINDOWS_OSTIMEREX_H_

#include "FreeRTOS.h"
#include "timers.h"

int32 OS_TimerGetIdByHostId(uint32 *timer_id, TimerHandle_t host_timer_id);
void OS_UsecsToTicks(uint32 usecs, TickType_t *ticks);

#endif /* OSAL_CORE_SRC_OS_FREERTOS_WINDOWS_OSTIMEREX_H_ */
