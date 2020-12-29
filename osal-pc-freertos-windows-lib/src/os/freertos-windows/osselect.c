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
 * \file   osselect.c
 * \author Christopher Sullivan based on work by joseph.p.hickey@nasa.gov
 *
 * Purpose: This file contains wrappers around the select() system call
 *
 */

/****************************************************************************************
 INCLUDE FILES
 ***************************************************************************************/

#include "os-FreeRTOS.h"

#ifdef OS_INCLUDE_NETWORK

/****************************************************************************************
 GLOBAL DATA
 ***************************************************************************************/

/*
 * The socket table.
 *
 * This is shared by all OSAL entities that perform low-level I/O.
 */
OS_FreeRTOS_socket_entry_t OS_impl_socket_table[OS_MAX_NUM_OPEN_FILES];

/*----------------------------------------------------------------
 * Function: OS_FdSet_ConvertIn_Impl
 *
 *  Purpose: Local helper routine, not part of OSAL API.
 *
 *          Convert an OS_FdSet (OSAL) structure into an SocketSet_t (FreeRTOS)
 *          which can then be passed to the FreeRTOS_select function.
 *
 * returns: Highest numbered file descriptor in the output SocketSet_t
 *-----------------------------------------------------------------*/
static void OS_FdSet_ConvertIn_Impl(SocketSet_t *os_set, OS_FdSet *OSAL_set, BaseType_t xSelectBits)
{
   uint32 offset;
   uint32 bit;
   uint32 id;
   uint8 objids;
   Socket_t osfd;

   for (offset = 0; offset < sizeof(OSAL_set->object_ids); ++offset)
   {
      objids = OSAL_set->object_ids[offset];
      bit = 0;
      while (objids != 0)
      {
         if (objids & 0x01)
         {
            id = (offset * 8) + bit;
            osfd = OS_impl_socket_table[id].socket;
            if (osfd != NULL)
            {
               FreeRTOS_FD_SET(osfd, os_set, xSelectBits);
            }
         }
         ++bit;
         objids >>= 1;
      }
   }
} /* end OS_FdSet_ConvertIn_Impl */

/*----------------------------------------------------------------
 * Function: OS_FdSet_ConvertOut_Impl
 *
 *  Purpose: Local helper routine, not part of OSAL API.
 *
 *          Convert a FreeRTOS SocketSet_t structure into an OSAL OS_FdSet
 *          which can then be returned back to the application.
 *
 *          This actually un-sets any bits in the "Input" parameter
 *          which are also set in the "output" parameter.
 *-----------------------------------------------------------------*/
static void OS_FdSet_ConvertOut_Impl(SocketSet_t *output, OS_FdSet *Input)
{
   uint32 offset;
   uint32 bit;
   uint32 id;
   uint8 objids;
   Socket_t osfd;

   for (offset = 0; offset < sizeof(Input->object_ids); ++offset)
   {
      objids = Input->object_ids[offset];
      bit = 0;
      while (objids != 0)
      {
         if (objids & 0x01)
         {
            id = (offset * 8) + bit;
            osfd = OS_impl_socket_table[id].socket;
            if (osfd == NULL || !FreeRTOS_FD_ISSET(osfd, output))
            {
               Input->object_ids[offset] &= ~(1 << bit);
            }
         }
         ++bit;
         objids >>= 1;
      }
   }
} /* end OS_FdSet_ConvertOut_Impl */

/*----------------------------------------------------------------
 * Function: OS_DoSelect
 *
 *  Purpose: Local helper routine, not part of OSAL API.
 *
 *          Actual implementation of FreeRTOS_select() call
 *          Used by SelectSingle and SelectMultiple implementations (below)
 *-----------------------------------------------------------------*/
static int32 OS_DoSelect(SocketSet_t set, int32 msecs)
{
   int os_status;
   int32 return_code;

   do
   {
      os_status = FreeRTOS_select(set, msecs);
   }
   while (os_status == -pdFREERTOS_ERRNO_EINTR); //Loop until complete without signal

   if (os_status < 0)
   {
      return_code = OS_ERROR;
   }
   else if (os_status == 0)
   {
      return_code = OS_ERROR_TIMEOUT;
   }
   else
   {
      return_code = OS_SUCCESS;
   }

   return return_code;
} /* end OS_DoSelect */

/****************************************************************************************
 SELECT API
 ***************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_SelectSingle_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SelectSingle_Impl(uint32 stream_id, uint32 *SelectFlags, int32 msecs)
{
	int32 return_code;
	SocketSet_t set;
	BaseType_t xSelectBits;

	if(*SelectFlags & OS_STREAM_STATE_READABLE)
	{
		xSelectBits = eSELECT_READ;
	}
	else if(*SelectFlags & OS_STREAM_STATE_WRITABLE)
	{
		xSelectBits = eSELECT_WRITE;
	}
	else
	{
		/* Nothing to check for, return immediately. */
		return OS_SUCCESS;
	}

	set = FreeRTOS_CreateSocketSet();

	FreeRTOS_FD_SET(OS_impl_socket_table[stream_id].socket, set, xSelectBits);

	return_code = OS_DoSelect(set, msecs);

	if (return_code == OS_SUCCESS)
	{
		 if (!FreeRTOS_FD_ISSET(OS_impl_socket_table[stream_id].socket, set))
		 {
			if(*SelectFlags & OS_STREAM_STATE_READABLE)
			{
				*SelectFlags &= ~OS_STREAM_STATE_READABLE;
			}
			else if(*SelectFlags & OS_STREAM_STATE_WRITABLE)
			{
				*SelectFlags &= ~OS_STREAM_STATE_WRITABLE;
			}
		 }
	}
	else
	{
		*SelectFlags = 0;
	}

	return return_code;
} /* end OS_SelectSingle_Impl */


/*----------------------------------------------------------------
 *
 * Function: OS_SelectMultiple_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SelectMultiple_Impl(OS_FdSet *ReadSet, OS_FdSet *WriteSet, int32 msecs)
{
	SocketSet_t rd_set;
	SocketSet_t wr_set;
	int32 rd_return_code = OS_SUCCESS;
	int32 wr_return_code = OS_SUCCESS;

	if (ReadSet != NULL)
	{
		rd_return_code = OS_ERROR;
		rd_set = FreeRTOS_CreateSocketSet();
		OS_FdSet_ConvertIn_Impl(rd_set, ReadSet, eSELECT_READ);
		rd_return_code = OS_DoSelect(rd_set, msecs);
		if(rd_return_code == OS_SUCCESS)
		{
			OS_FdSet_ConvertOut_Impl(rd_set, ReadSet);
		}
	}

	if (WriteSet != NULL)
	{
		wr_return_code = OS_ERROR;
		wr_set = FreeRTOS_CreateSocketSet();
		OS_FdSet_ConvertIn_Impl(wr_set, WriteSet, eSELECT_WRITE);
		wr_return_code = OS_DoSelect(wr_set, msecs);
		if(wr_return_code == OS_SUCCESS)
		{
			OS_FdSet_ConvertOut_Impl(wr_set, ReadSet);
		}
	}

	if(rd_return_code == OS_SUCCESS && wr_return_code == OS_SUCCESS)
	{
		return OS_SUCCESS;
	}
	else
	{
		return OS_ERROR;
	}
} /* end OS_SelectMultiple_Impl */
#else

/****************************************************************************************
 SELECT API
 ***************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_SelectSingle_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SelectSingle_Impl(uint32 stream_id, uint32 *SelectFlags, int32 msecs)
{
	return OS_ERR_NOT_IMPLEMENTED;
} /* end OS_SelectSingle_Impl */


/*----------------------------------------------------------------
 *
 * Function: OS_SelectMultiple_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SelectMultiple_Impl(OS_FdSet *ReadSet, OS_FdSet *WriteSet, int32 msecs)
{
	return OS_ERR_NOT_IMPLEMENTED;
} /* end OS_SelectMultiple_Impl */

#endif

