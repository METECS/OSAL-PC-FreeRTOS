#include "common_types.h"
#include "osapi.h"
#include "osconfig.h"
#include "osapi-os-core.h"

// TODO: This file needs to be fully fleshed out with the functionality of the current OSAL
// TODO: The current OSAL adds a lot of functionality in this shared folder that should be used by the OSAL implementation for FreeRTOS

/*----------------------------------------------------------------
 *
 * Function: OS_ConvertToArrayIndex
 *
 *  Purpose: Implemented per public OSAL API
 *           See description in API and header file for detail
 *
 *-----------------------------------------------------------------*/
int32 OS_ConvertToArrayIndex(uint32 object_id, uint32 *ArrayIndex)
{
	// TODO: This rudimentary implementation should be fleshed as provided in the current OSAL
	*ArrayIndex = object_id;
    return OS_SUCCESS;
} /* end OS_ConvertToArrayIndex */
