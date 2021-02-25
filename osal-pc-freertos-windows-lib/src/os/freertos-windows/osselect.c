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
OS_FreeRTOS_filehandle_entry_t OS_impl_filehandle_table[OS_MAX_NUM_OPEN_FILES];

/****************************************************************************************
 HELPER FUNCTION
 ***************************************************************************************/
void UpdateConnectionStatus(uint32);
void UpdateConnectionStatus(uint32 stream_id)
{
	bool isConnected = FreeRTOS_issocketconnected(OS_impl_filehandle_table[stream_id].fd);
	if(isConnected)
	{
		OS_impl_filehandle_table[stream_id].disconnected = false;
	}
	else
	{
		if(OS_impl_filehandle_table[stream_id].connected)
		{
			OS_impl_filehandle_table[stream_id].disconnected = true;
		}
	}
	OS_impl_filehandle_table[stream_id].connected = isConnected;
}

/*----------------------------------------------------------------
 * Function: OS_FdSet_ConvertIn_Impl
 *
 *  Purpose: Local helper routine, not part of OSAL API.
 *
 *          Convert an OS_FdSet (OSAL) structure into an SocketSet_t (FreeRTOS)
 *          which can then be passed to the FreeRTOS_select function.
 *
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
            osfd = OS_impl_filehandle_table[id].fd;
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
static void OS_FdSet_ConvertOut_Impl(SocketSet_t *output, OS_FdSet *Input, BaseType_t xSelectBits, bool *disconn)
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
            osfd = OS_impl_filehandle_table[id].fd;
            if(osfd == NULL)
            {
        		Input->object_ids[offset] &= ~(1 << bit);
            }
            else
            {
				UpdateConnectionStatus(id);
				//Disconnected sockets should always be selected. They either have an event pending or they need to signal that they are done
				if(OS_impl_filehandle_table[id].disconnected)
				{
					*disconn = true;
				}
				else if(!FreeRTOS_FD_ISSET(osfd, output))
				{
					Input->object_ids[offset] &= ~(1 << bit);
				}

                FreeRTOS_FD_CLR(osfd, output, xSelectBits);
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
   TickType_t ticks;

   if(msecs>=0)
   {
	   OS_UsecsToTicks(msecs*1000, &ticks);
   }
   else
   {
	   ticks = portMAX_DELAY;
   }

   do
   {
      os_status = FreeRTOS_select(set, ticks);
   }
   while (os_status == -pdFREERTOS_ERRNO_EINTR);

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
	BaseType_t xSelectBits = 0;
	BaseType_t returnedBits;

	if (*SelectFlags != 0)
	{
		set = FreeRTOS_CreateSocketSet();

		if(*SelectFlags & OS_STREAM_STATE_READABLE)
		{
			xSelectBits |= eSELECT_READ;
		}
		if(*SelectFlags & OS_STREAM_STATE_WRITABLE)
		{
			xSelectBits |= eSELECT_WRITE;
		}

		FreeRTOS_FD_SET(OS_impl_filehandle_table[stream_id].fd, set, xSelectBits);

		return_code = OS_DoSelect(set, msecs);

		UpdateConnectionStatus(stream_id);

		if (return_code == OS_SUCCESS)
		{
			returnedBits = FreeRTOS_FD_ISSET(OS_impl_filehandle_table[stream_id].fd, set);
			if(!(returnedBits & eSELECT_READ))
			{
				*SelectFlags &= ~OS_STREAM_STATE_READABLE;
			}
			if(!(returnedBits & eSELECT_WRITE))
			{
				*SelectFlags &= ~OS_STREAM_STATE_WRITABLE;
			}
		}
		else if (return_code == OS_ERROR_TIMEOUT && OS_impl_filehandle_table[stream_id].disconnected)
		{
			//This will be a problem if OS_PEND is used!
			return_code = OS_SUCCESS;
			if(!(xSelectBits & eSELECT_READ))
			{
				*SelectFlags &= ~OS_STREAM_STATE_READABLE;
			}
			if(!(xSelectBits & eSELECT_WRITE))
			{
				*SelectFlags &= ~OS_STREAM_STATE_WRITABLE;
			}
		}
		else
		{
			*SelectFlags = 0;
		}

		FreeRTOS_FD_CLR(OS_impl_filehandle_table[stream_id].fd, set, xSelectBits);
	}
	else
	{
		/* Nothing to check for, return immediately. */
		return_code = OS_SUCCESS;
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
	SocketSet_t set;
	int32 return_code = OS_SUCCESS;
	bool wr_disconn = false;
	bool rd_disconn = false;

	set = FreeRTOS_CreateSocketSet();

	if (ReadSet != NULL)
	{
		OS_FdSet_ConvertIn_Impl(set, ReadSet, eSELECT_READ);
	}
	if (WriteSet != NULL)
	{
		OS_FdSet_ConvertIn_Impl(set, WriteSet, eSELECT_WRITE);
	}

	return_code = OS_DoSelect(set, msecs);

	if(return_code != OS_ERROR)
	{
		if (ReadSet != NULL)
		{
			OS_FdSet_ConvertOut_Impl(set, ReadSet, eSELECT_READ, &rd_disconn);
		}
		if (WriteSet != NULL)
		{
			OS_FdSet_ConvertOut_Impl(set, WriteSet, eSELECT_WRITE, &wr_disconn);
		}

		if(rd_disconn || wr_disconn)
		{
			return_code = OS_SUCCESS;
		}
	}

	return return_code;
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

