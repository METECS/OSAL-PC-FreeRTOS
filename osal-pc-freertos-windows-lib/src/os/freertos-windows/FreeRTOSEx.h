/*
 * FreeRTOSHelper.h
 *
 *  Created on: Jan 28, 2019
 *      Author: Jonathan Brandenburg
 */

#ifndef OSAL_CORE_SRC_OS_FREERTOS_WINDOWS_FREERTOSEX_H_
#define OSAL_CORE_SRC_OS_FREERTOS_WINDOWS_FREERTOSEX_H_

#include "FreeRTOS.h"

TickType_t getElapsedSeconds();
TickType_t getElapsedMicroseconds();

#endif /* OSAL_CORE_SRC_OS_FREERTOS_WINDOWS_FREERTOSEX_H_ */
