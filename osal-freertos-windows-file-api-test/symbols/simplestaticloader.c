/*
 * simplestaticloader.c
 *
 *  Created on: Feb 1, 2019
 *      Author: Jonathan Brandenburg
 */

#include "simplestaticloader.h"

#include <string.h>

#include "simplestaticloader.inc"

unsigned char SimpleStaticLoadFile(char *translated_path, static_load_file_header_t *symbol_entry) {
	/*
	 * Determine the number of known symbols
	 */
	int symbol_count = sizeof(known_symbols) / sizeof(known_symbols[0]);

	/*
	 * Find the desired file path in the list of known symbols
	 */
	int i;
	for (i = 0; i < symbol_count; ++i) {
		if (strcmp (translated_path, known_symbols[i].module_name) == 0) {
			break;
		}
	}

	/*
	 * If file path was not found...
	 */
	if (i >= symbol_count) {
		return 0;
	}

	/*
	 * Populate the symbol entry parameter
	 */
	strncpy(symbol_entry->module_name, known_symbols[i].module_name, OS_MAX_LOCAL_PATH_LEN);
	symbol_entry->module_name[OS_MAX_LOCAL_PATH_LEN-1] = '\0';
	strncpy(symbol_entry->entry_point_name, known_symbols[i].entry_point_name, OS_MAX_LOCAL_PATH_LEN);
	symbol_entry->entry_point_name[OS_MAX_LOCAL_PATH_LEN-1] = '\0';
	symbol_entry->entry_point = known_symbols[i].entry_point;
	symbol_entry->code_target = known_symbols[i].code_target;
	symbol_entry->code_size = known_symbols[i].code_size;
	symbol_entry->data_target = known_symbols[i].data_target;
	symbol_entry->data_size = known_symbols[i].data_size;
	symbol_entry->bss_target = known_symbols[i].bss_target;
	symbol_entry->bss_size = known_symbols[i].bss_size;
	symbol_entry->flags = known_symbols[i].flags;
	return 1;
}
