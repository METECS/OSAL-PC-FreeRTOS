/*
 * Added some functionality to keep a clock based on ticks and seconds
 *
 * This file is based on FreeRTOS/Demo/WIN32-MSVC/main.c in the FreeRTOS V10.1.1
 * package with the following license:
 *     Copyright (C) 2018 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 *     Permission is hereby granted, free of charge, to any person obtaining a copy of
 *     this software and associated documentation files (the "Software"), to deal in
 *     the Software without restriction, including without limitation the rights to
 *     use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 *     the Software, and to permit persons to whom the Software is furnished to do so,
 *     subject to the following conditions:
 *
 *     The above copyright notice and this permission notice shall be included in all
 *     copies or substantial portions of the Software.
 *
 *     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *     IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 *     FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 *     COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 *     IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *     CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *     http://www.FreeRTOS.org
 *     http://aws.amazon.com/freertos
 */

/******************************************************************************
 *
 * This file implements the code that is not demo specific, including the
 * hardware setup and FreeRTOS hook functions.
 *
 *******************************************************************************
 * NOTE: Windows will not be running the FreeRTOS demo threads continuously, so
 * do not expect to get real time behaviour from the FreeRTOS Windows port, or
 * this demo application.  Also, the timing information in the FreeRTOS+Trace
 * logs have no meaningful units.  See the documentation page for the Windows
 * port for further information:
 * http://www.freertos.org/FreeRTOS-Windows-Simulator-Emulator-for-Visual-Studio-and-Eclipse-MingW.html
 *

 *
 *******************************************************************************
 */

/* Standard includes. */
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>

/* FreeRTOS kernel includes. */
#include "FreeRTOS.h"
#include "task.h"

static TickType_t elapsed_seconds = 0;
static TickType_t elapsed_ticks = 0;

#ifndef configFREERTOS_RUN_AS_SIM
#error configFREERTOS_RUN_AS_SIM must be set to 0 or 1 in FreeRTOSConfig.h
#endif
#if configFREERTOS_RUN_AS_SIM == 1
extern HANDLE freertos_sync_pipe;
#endif

/*
 * Prototypes for the standard FreeRTOS application hook (callback) functions
 * implemented within this file.  See http://www.freertos.org/a00016.html .
 */
void vApplicationMallocFailedHook(void);
void vApplicationIdleHook(void);
void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName);
void vApplicationTickHook(void);
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
		StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize);
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
		StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize);

/*-----------------------------------------------------------*/

/* When configSUPPORT_STATIC_ALLOCATION is set to 1 the application writer can
 use a callback function to optionally provide the memory required by the idle
 and timer tasks.  This is the stack that will be used by the timer task.  It is
 declared here, as a global, so it can be checked by a test that is implemented
 in a different file. */
StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

/*-----------------------------------------------------------*/

void vApplicationMallocFailedHook(void) {
	/* vApplicationMallocFailedHook() will only be called if
	 configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h.  It is a hook
	 function that will get called if a call to pvPortMalloc() fails.
	 pvPortMalloc() is called internally by the kernel whenever a task, queue,
	 timer or semaphore is created.  It is also called by various parts of the
	 demo application.  If heap_1.c, heap_2.c or heap_4.c is being used, then the
	 size of the	heap available to pvPortMalloc() is defined by
	 configTOTAL_HEAP_SIZE in FreeRTOSConfig.h, and the xPortGetFreeHeapSize()
	 API function can be used to query the size of free heap space that remains
	 (although it does not provide information on how the remaining heap might be
	 fragmented).  See http://www.freertos.org/a00111.html for more
	 information. */
	vAssertCalled( __LINE__, __FILE__);
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook(void) {
	/* vApplicationIdleHook() will only be called if configUSE_IDLE_HOOK is set
	 to 1 in FreeRTOSConfig.h.  It will be called on each iteration of the idle
	 task.  It is essential that code added to this hook function never attempts
	 to block in any way (for example, call xQueueReceive() with a block time
	 specified, or call vTaskDelay()).  If application tasks make use of the
	 vTaskDelete() API function to delete themselves then it is also important
	 that vApplicationIdleHook() is permitted to return to its calling function,
	 because it is the responsibility of the idle task to clean up memory
	 allocated by the kernel to any task that has since deleted itself. */

	/* Uncomment the following code to allow the trace to be stopped with any
	 key press.  The code is commented out by default as the kbhit() function
	 interferes with the run time behaviour. */
	/*
	 if( _kbhit() != pdFALSE )
	 {
	 if( xTraceRunning == pdTRUE )
	 {
	 vTraceStop();
	 prvSaveTraceFile();
	 xTraceRunning = pdFALSE;
	 }
	 }
	 */
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName) {
	(void) pcTaskName;
	(void) pxTask;

	/* Run time stack overflow checking is performed if
	 configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
	 function is called if a stack overflow is detected.  This function is
	 provided as an example only as stack overflow checking does not function
	 when running the FreeRTOS Windows port. */
	vAssertCalled( __LINE__, __FILE__);
}
/*-----------------------------------------------------------*/

#ifndef configFREERTOS_RUN_AS_SIM
#error configFREERTOS_RUN_AS_SIM must be set to 0 or 1 in FreeRTOSConfig.h
#endif
#if configFREERTOS_RUN_AS_SIM == 1
void vApplicationSyncHook(void) {

#ifndef configFREERTOS_SIM_WARMUP_MS
#error configFREERTOS_SIM_WARMUP_MS must be set in FreeRTOSConfig.h
#endif
#ifndef configFREERTOS_SIM_MS_BETWEEN_SYNCS
#error configFREERTOS_SIM_MS_BETWEEN_SYNCS must be set in FreeRTOSConfig.h
#endif

	/*
	 * The following functionality is used to "pause" each tick to synchronize
	 * execution with an external application, such as a simulation driver
	 */

	/*
	 * We have a setting to allow the application to start up and stabilize.
	 * I'm doing this to allow the network events to fire but there
	 * are other plausible reasons not to begin synchronizing the application
	 * until it has been running a certain period of time
	 */
	static TickType_t last_sync_tick = 0;

	TickType_t current_ticks = xTaskGetTickCountFromISR();
	if (current_ticks
			>= last_sync_tick + configFREERTOS_SIM_MS_BETWEEN_SYNCS / portTICK_PERIOD_MS) {
		last_sync_tick += configFREERTOS_SIM_MS_BETWEEN_SYNCS / portTICK_PERIOD_MS;

		if (current_ticks >= configFREERTOS_SIM_WARMUP_MS / portTICK_PERIOD_MS) {
			/*
			 * Must NOT block in this section. Otherwise, other threads might execute.
			 * This is particularly a risk in the Windows simulator in which tasks
			 * are emulated with threads.
			 */

			if (freertos_sync_pipe != INVALID_HANDLE_VALUE) {
				static DWORD target_execution_progress_ms = 0;
				/*
				 * If the FreeRTOS application has reached its target execution
				 * time, notify the other application by writing our current
				 * execution time
				 */
				DWORD current_execution_ms = current_ticks * portTICK_PERIOD_MS;
				if (current_execution_ms >= target_execution_progress_ms) {
					/*
					 * Write the current execution time, in ms, to the pipe. This activity
					 * simultaneously notifies the synchronizing application
					 * the FreeRTOS application is waiting to progress and let's
					 * the synchronizing application know how far it has
					 * progressed so far.
					 * If the write fails, just proceed and hope the synchronization
					 * is successful next time.
					 */
					WriteFile(freertos_sync_pipe, &current_execution_ms, sizeof(current_execution_ms), NULL, NULL);

					/*
					 * Now wait for the synchronizing application to tell us to proceed.
					 * To synchronize, we wait for an acknowledgment of our current progress.
					 * The value we read from the pipe is the desired millisecond count we should
					 * execute to. The FreeRTOS application will advance to just that point.
					 * We will overshoot if the sync resolution is larger than 1.
					 */
					for (;;) {
						DWORD bytesRead;
						if (ReadFile(freertos_sync_pipe, &target_execution_progress_ms, sizeof(target_execution_progress_ms), &bytesRead, NULL)) {
							if (bytesRead > 0) {
								break;
							}
						}
					}
				}
			}
		}
	}
}
#endif

void vApplicationTickHook(void) {
	/* This function will be called by each tick interrupt if
	 configUSE_TICK_HOOK is set to 1 in FreeRTOSConfig.h.  User code can be
	 added here, but the tick hook is called from an interrupt context, so
	 code must not attempt to block, and only the interrupt safe FreeRTOS API
	 functions can be used (those that end in FromISR()). */

#if configFREERTOS_RUN_AS_SIM == 1
		vApplicationSyncHook();
#endif

	// Keep a clock that won't rollover as quickly as just keeping a count of
	// ticks.
	// Thus, keep a separate count of elapsed seconds and elapsed_ticks
	// is the number of ticks since the last full second.
	elapsed_ticks++;
	while (elapsed_ticks >= configTICK_RATE_HZ) {
		elapsed_ticks -= configTICK_RATE_HZ;
		elapsed_seconds++;
	}
}

TickType_t getElapsedSeconds() {
	return elapsed_seconds;
}

TickType_t getElapsedMicroseconds() {
	// Deal with overflow. Ensure no intermediate value is > 2^32
#if configTICK_RATE_HZ <= 1000
	return (elapsed_ticks * 1000000) / configTICK_RATE_HZ; /* could be some error if configTICK_RATE_HZ is not an even divisor of 1000000 */
#elif configTICK_RATE_HZ < 1000000
	return elapsed_ticks * (1000000 / configTICK_RATE_HZ); /* could be substantial error if configTICK_RATE_HZ is not an even divisor of 1000000 */
#elif configTICK_RATE_HZ == 1000000
	return elapsed_ticks;
#else
	return elapsed_ticks / (configTICK_RATE_HZ / 1000000); /* could be substantial error if configTICK_RATE_HZ is not divisible by 1000000 */
#endif
}

/*-----------------------------------------------------------*/

void vApplicationDaemonTaskStartupHook(void) {
	/* This function will be called once only, when the daemon task starts to
	 execute	(sometimes called the timer task).  This is useful if the
	 application includes initialisation code that would benefit from executing
	 after the scheduler has been started. */
}
/*-----------------------------------------------------------*/

void vAssertCalled(unsigned long ulLine, const char * const pcFileName) {
	static BaseType_t xPrinted = pdFALSE;
	volatile uint32_t ulSetToNonZeroInDebuggerToContinue = 0;

	/* Called if an assertion passed to configASSERT() fails.  See
	 http://www.freertos.org/a00110.html#configASSERT for more information. */

	/* Parameters are not used. */
	(void) ulLine;
	(void) pcFileName;

	taskENTER_CRITICAL();
	{
		/* Stop the trace recording. */
		if (xPrinted == pdFALSE) {
			xPrinted = pdTRUE;
			//			if( xTraceRunning == pdTRUE )
			//			{
			//				prvSaveTraceFile();
			//			}
		}

		/* You can step out of this function to debug the assertion by using
		 the debugger to set ulSetToNonZeroInDebuggerToContinue to a non-zero
		 value. */
		while (ulSetToNonZeroInDebuggerToContinue == 0) {
			__asm volatile( "NOP" );
			__asm volatile( "NOP" );
		}
	}
	taskEXIT_CRITICAL();
}
/*-----------------------------------------------------------*/

//static void prvSaveTraceFile( void )
//{
//	/* Tracing is not used when code coverage analysis is being performed. */
//	#if( projCOVERAGE_TEST != 1 )
//	{
//		FILE* pxOutputFile;
//
//		vTraceStop();
//
//		pxOutputFile = fopen( "Trace.dump", "wb");
//
//		if( pxOutputFile != NULL )
//		{
//			fwrite( RecorderDataPtr, sizeof( RecorderDataType ), 1, pxOutputFile );
//			fclose( pxOutputFile );
//			printf( "\r\nTrace output saved to Trace.dump\r\n" );
//		}
//		else
//		{
//			printf( "\r\nFailed to create trace dump file\r\n" );
//		}
//	}
//	#endif
//}
/*-----------------------------------------------------------*/

/* configUSE_STATIC_ALLOCATION is set to 1, so the application must provide an
 implementation of vApplicationGetIdleTaskMemory() to provide the memory that is
 used by the Idle task. */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
		StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize) {
	/* If the buffers to be provided to the Idle task are declared inside this
	 function then they must be declared static - otherwise they will be allocated on
	 the stack and so not exists after this function exits. */
	static StaticTask_t xIdleTaskTCB;
	static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];

	/* Pass out a pointer to the StaticTask_t structure in which the Idle task's
	 state will be stored. */
	*ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

	/* Pass out the array that will be used as the Idle task's stack. */
	*ppxIdleTaskStackBuffer = uxIdleTaskStack;

	/* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
	 Note that, as the array is necessarily of type StackType_t,
	 configMINIMAL_STACK_SIZE is specified in words, not bytes. */
	*pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
/*-----------------------------------------------------------*/

/* configUSE_STATIC_ALLOCATION and configUSE_TIMERS are both set to 1, so the
 application must provide an implementation of vApplicationGetTimerTaskMemory()
 to provide the memory that is used by the Timer service task. */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
		StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize) {
	/* If the buffers to be provided to the Timer task are declared inside this
	 function then they must be declared static - otherwise they will be allocated on
	 the stack and so not exists after this function exits. */
	static StaticTask_t xTimerTaskTCB;

	/* Pass out a pointer to the StaticTask_t structure in which the Timer
	 task's state will be stored. */
	*ppxTimerTaskTCBBuffer = &xTimerTaskTCB;

	/* Pass out the array that will be used as the Timer task's stack. */
	*ppxTimerTaskStackBuffer = uxTimerTaskStack;

	/* Pass out the size of the array pointed to by *ppxTimerTaskStackBuffer.
	 Note that, as the array is necessarily of type StackType_t,
	 configMINIMAL_STACK_SIZE is specified in words, not bytes. */
	*pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

