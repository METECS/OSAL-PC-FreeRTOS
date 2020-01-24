/*
 * simplestaticloader.h
 *
 *  Created on: Feb 1, 2019
 *      Author: Jonathan Brandenburg
 */

#ifndef OSAL_CORE_SRC_OS_FREERTOS_WINDOWS_FREERTOS_SOURCE_SIMPLESTATICLOADER_H_
#define OSAL_CORE_SRC_OS_FREERTOS_WINDOWS_FREERTOS_SOURCE_SIMPLESTATICLOADER_H_

#include "common_types.h"
#include "osapi.h"

typedef struct
{
	char module_name[OS_MAX_LOCAL_PATH_LEN];
	char entry_point_name[OS_MAX_LOCAL_PATH_LEN];
	cpuaddr entry_point;
	cpuaddr code_target;
	cpuaddr code_size;
	cpuaddr data_target;
	cpuaddr data_size;
	cpuaddr bss_target;
	cpuaddr bss_size;
	uint32 flags;
} static_load_file_header_t;

uint32 GetSymbolCount();
unsigned char SimpleStaticLoadFile(char *translated_path, static_load_file_header_t *symbol_entry);

#endif /* OSAL_CORE_SRC_OS_FREERTOS_WINDOWS_FREERTOS_SOURCE_SIMPLESTATICLOADER_H_ */
