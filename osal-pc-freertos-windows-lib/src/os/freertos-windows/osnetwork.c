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
 * \file   osnetwork.c
 * \author Christopher Sullivan based on work by joseph.p.hickey@nasa.gov
 *
 * Purpose: This file contains the network functionality for the osapi.
 */

/****************************************************************************************
 INCLUDE FILES
 ***************************************************************************************/
#include "os-FreeRTOS.h"

#ifdef OS_INCLUDE_NETWORK

/****************************************************************************************
 DEFINES
 ****************************************************************************************/

typedef union
{
   struct freertos_sockaddr freertos_sockaddr;
} OS_freertos_sockaddr_Accessor_t;

/****************************************************************************************
 GLOBAL DATA
 ***************************************************************************************/

/*
 * The file handle table.
 * This is shared by all OSAL entities that perform low-level I/O.
 */
OS_FreeRTOS_filehandle_entry_t OS_impl_filehandle_table[OS_MAX_NUM_OPEN_FILES];

/****************************************************************************************
 EXTERNAL DECLARATIONS
 ***************************************************************************************/

/****************************************************************************************
 Network API
 ***************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_NetworkGetHostName_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_NetworkGetHostName_Impl(char *host_name, uint32 name_len)
{
    return OS_ERR_NOT_IMPLEMENTED;
} /* end OS_NetworkGetHostName_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_NetworkGetID_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_NetworkGetID_Impl          (int32 *IdBuf)
{
    return OS_ERR_NOT_IMPLEMENTED;
} /* end OS_NetworkGetID_Impl */

/****************************************************************************************
 Socket API
 ***************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_SocketOpen_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SocketOpen_Impl(uint32 sock_id)
{
	BaseType_t os_domain;
	BaseType_t os_type;
	BaseType_t os_proto;

	os_proto = 0;

	switch(OS_stream_table[sock_id].socket_type)
	{
	case OS_SocketType_DATAGRAM:
	  os_type = FREERTOS_SOCK_DGRAM;
	  break;
	case OS_SocketType_STREAM:
	  os_type = FREERTOS_SOCK_STREAM;
	  break;

	default:
	  return OS_ERR_NOT_IMPLEMENTED;
	}

	switch(OS_stream_table[sock_id].socket_domain)
	{
	case OS_SocketDomain_INET:
	  os_domain = FREERTOS_AF_INET;
	  break;
	default:
	  return OS_ERR_NOT_IMPLEMENTED;
	}

	switch(OS_stream_table[sock_id].socket_domain)
	{
	case OS_SocketDomain_INET:
	case OS_SocketDomain_INET6:
	  switch(OS_stream_table[sock_id].socket_type)
	  {
	  case OS_SocketType_DATAGRAM:
		 os_proto = FREERTOS_IPPROTO_UDP;
		 break;
	  case OS_SocketType_STREAM:
		 os_proto = FREERTOS_IPPROTO_TCP;
		 break;
	  default:
		 break;
	  }
	  break;
	default:
	  break;
	}

	OS_impl_filehandle_table[sock_id].fd = FreeRTOS_socket(os_domain, os_type, os_proto);
	if (OS_impl_filehandle_table[sock_id].fd == FREERTOS_INVALID_SOCKET)
	{
	   //Insufficient FreeRTOS heap memory
		OS_impl_filehandle_table[sock_id].fd = NULL;
		return OS_ERROR;
	}
	OS_impl_filehandle_table[sock_id].selectable = true;

	return OS_SUCCESS;
} /* end OS_SocketOpen_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_SocketBind_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SocketBind_Impl(uint32 sock_id, const OS_SockAddr_t *Addr)
{
   int os_result;
   socklen_t addrlen;
   struct freertos_sockaddr *sa;

   sa = (struct freertos_sockaddr *)Addr->AddrData;

   switch(sa->sin_family)
   {
   case FREERTOS_AF_INET:
      addrlen = sizeof(struct freertos_sockaddr);
      break;
   default:
      addrlen = 0;
      break;
   }

   if (addrlen == 0 || addrlen > OS_SOCKADDR_MAX_LEN)
   {
      return OS_ERR_BAD_ADDRESS;
   }

   os_result = FreeRTOS_bind(OS_impl_filehandle_table[sock_id].fd, sa, addrlen);
   if (os_result < 0)
   {
      return OS_ERROR;
   }

   /* Start listening on the socket (implied for stream sockets) */
   if (OS_stream_table[sock_id].socket_type == OS_SocketType_STREAM)
   {
      os_result = FreeRTOS_listen(OS_impl_filehandle_table[sock_id].fd, 10);
      if (os_result < 0)
      {
         return OS_ERROR;
      }
   }
   return OS_SUCCESS;
} /* end OS_SocketBind_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_SocketConnect_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SocketConnect_Impl(uint32 sock_id, const OS_SockAddr_t *Addr, int32 timeout)
{
   int32 return_code;
   int os_status;
   socklen_t slen;
   struct freertos_sockaddr *sa;

   sa = (struct freertos_sockaddr *)Addr->AddrData;
   switch(sa->sin_family)
   {
   case FREERTOS_AF_INET:
      slen = sizeof(struct freertos_sockaddr);
      break;
   default:
      slen = 0;
      break;
   }

   if (slen != Addr->ActualLength)
   {
      return_code = OS_ERR_BAD_ADDRESS;
   }
   else
   {
       return_code = OS_SUCCESS;
       os_status = FreeRTOS_connect(OS_impl_filehandle_table[sock_id].fd, sa, slen);
       if (os_status < 0)
       {
           if (os_status != -pdFREERTOS_ERRNO_EINPROGRESS && os_status != -pdFREERTOS_ERRNO_EWOULDBLOCK)
           {
               return_code = OS_ERROR;
           }
           else
           {
				//Checking if it is writable does not guarantee that the 3-Way handshake has been completed
				//Check the connection every 1 millisecond to see when it has been connected
				uint32 elapsedTime = 0;
				TickType_t ticks = OS_Milli2Ticks(1);
				while(elapsedTime<=timeout)
				{
					if(FreeRTOS_issocketconnected(OS_impl_filehandle_table[sock_id].fd))
					{
					OS_impl_filehandle_table[sock_id].connected = true;
					return_code = OS_SUCCESS;
					break;
					}

					vTaskDelay(ticks);
					elapsedTime++;
				}
				if(elapsedTime>timeout)
				{
					return_code = OS_ERROR_TIMEOUT;
				}
           }
       }
   }
   return return_code;
} /* end OS_SocketConnect_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_SocketAccept_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SocketAccept_Impl(uint32 sock_id, uint32 connsock_id, OS_SockAddr_t *Addr, int32 timeout)
{
   int32 return_code;
   uint32 operation;
   socklen_t addrlen;

   operation = OS_STREAM_STATE_READABLE;
   return_code = OS_SelectSingle_Impl(sock_id, &operation, timeout);
   if (return_code == OS_SUCCESS)
   {
      if ((operation & OS_STREAM_STATE_READABLE) == 0)
      {
         return_code = OS_ERROR_TIMEOUT;
      }
      else
      {
         addrlen = Addr->ActualLength;
         OS_impl_filehandle_table[connsock_id].fd = FreeRTOS_accept(OS_impl_filehandle_table[sock_id].fd, (struct freertos_sockaddr *)Addr->AddrData, &addrlen);
         if (OS_impl_filehandle_table[connsock_id].fd == NULL || OS_impl_filehandle_table[connsock_id].fd == FREERTOS_INVALID_SOCKET )
         {
            return_code = OS_ERROR;
         }
         else
         {
             Addr->ActualLength = addrlen;
             OS_impl_filehandle_table[connsock_id].selectable = true;
             OS_impl_filehandle_table[connsock_id].connected = true;
         }
      }
   }

   return return_code;
} /* end OS_SocketAccept_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_SocketRecvFrom_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SocketRecvFrom_Impl(uint32 sock_id, void *buffer, uint32 buflen, OS_SockAddr_t *RemoteAddr, int32 timeout)
{
   int32 return_code;
   int os_result;
   uint32 operation;
   struct freertos_sockaddr *sa;
   socklen_t addrlen;

   if (RemoteAddr == NULL)
   {
      sa = NULL;
      addrlen = 0;
   }
   else
   {
      addrlen = OS_SOCKADDR_MAX_LEN;
      //In FreeRTOS, sin_family doesn't seem to be copied in FreeRTOS_recvfrom.
      //Init address to ensure it is included.
      //If other families are included in the future(IPV6), this will need to change.
      OS_SocketAddrInit_Impl(RemoteAddr, OS_SocketDomain_INET);
      sa = (struct freertos_sockaddr *)RemoteAddr->AddrData;
   }

   operation = OS_STREAM_STATE_READABLE;
   return_code = OS_SelectSingle_Impl(sock_id, &operation, timeout);
   if (return_code == OS_SUCCESS)
   {
      if ((operation & OS_STREAM_STATE_READABLE) == 0)
      {
         return_code = OS_ERROR_TIMEOUT;
      }
      else
      {
         os_result = FreeRTOS_recvfrom(OS_impl_filehandle_table[sock_id].fd, buffer, buflen, 0, sa, &addrlen);
         if (os_result < 0)
         {
		   return_code = OS_ERROR;
         }
         else
         {
            return_code = os_result;

            if (RemoteAddr != NULL)
            {
               RemoteAddr->ActualLength = addrlen;
            }
         }
      }
   }

   return return_code;
} /* end OS_SocketRecvFrom_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_SocketSendTo_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SocketSendTo_Impl(uint32 sock_id, const void *buffer, uint32 buflen, const OS_SockAddr_t *RemoteAddr)
{
   int os_result;
   socklen_t addrlen;
   const struct freertos_sockaddr *sa;

   sa = (const struct freertos_sockaddr *)RemoteAddr->AddrData;
   switch(sa->sin_family)
   {
   case FREERTOS_AF_INET:
      addrlen = sizeof(struct freertos_sockaddr);
      break;
   default:
      addrlen = 0;
      break;
   }

   if (addrlen != RemoteAddr->ActualLength)
   {
      return OS_ERR_BAD_ADDRESS;
   }

   os_result = FreeRTOS_sendto(OS_impl_filehandle_table[sock_id].fd, buffer, buflen, 0, sa, addrlen);
   if (os_result == 0)
   {
      return OS_ERROR;
   }

   return os_result;
} /* end OS_SocketSendTo_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_SocketGetInfo_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SocketGetInfo_Impl (uint32 sock_id, OS_socket_prop_t *sock_prop)
{
   return OS_SUCCESS;
} /* end OS_SocketGetInfo_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_SocketAddrInit_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SocketAddrInit_Impl(OS_SockAddr_t *Addr, OS_SocketDomain_t Domain)
{
   uint8_t sin_family;
   socklen_t addrlen;
   OS_freertos_sockaddr_Accessor_t *Accessor;

   memset(Addr, 0, sizeof(OS_SockAddr_t));
   Accessor = (OS_freertos_sockaddr_Accessor_t *)Addr->AddrData;

   switch(Domain)
   {
   case OS_SocketDomain_INET:
      sin_family = FREERTOS_AF_INET;
      addrlen = sizeof(struct freertos_sockaddr);
      break;
   default:
      addrlen = 0;
      break;
   }

   if (addrlen == 0 || addrlen > OS_SOCKADDR_MAX_LEN)
   {
      return OS_ERR_NOT_IMPLEMENTED;
   }

   Addr->ActualLength = addrlen;
   Accessor->freertos_sockaddr.sin_family = sin_family;

   return OS_SUCCESS;
} /* end OS_SocketAddrInit_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_SocketAddrToString_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SocketAddrToString_Impl(char *buffer, uint32 buflen, const OS_SockAddr_t *Addr)
{
   const OS_freertos_sockaddr_Accessor_t *Accessor;

   Accessor = (const OS_freertos_sockaddr_Accessor_t *)Addr->AddrData;

   switch(Accessor->freertos_sockaddr.sin_family)
   {
   case FREERTOS_AF_INET:
      break;
   default:
      return OS_ERR_BAD_ADDRESS;
      break;
   }

   FreeRTOS_inet_ntoa(Accessor->freertos_sockaddr.sin_addr, buffer);

   return OS_SUCCESS;
} /* end OS_SocketAddrToString_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_SocketAddrFromString_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SocketAddrFromString_Impl(OS_SockAddr_t *Addr, const char *string)
{
   OS_freertos_sockaddr_Accessor_t *Accessor;

   Accessor = (OS_freertos_sockaddr_Accessor_t *)Addr->AddrData;

   switch(Accessor->freertos_sockaddr.sin_family)
   {
   case FREERTOS_AF_INET:
      break;
   default:
      return OS_ERR_BAD_ADDRESS;
      break;
   }

   Accessor->freertos_sockaddr.sin_addr = FreeRTOS_inet_addr(string);

   if(Accessor->freertos_sockaddr.sin_addr == 0)
   {
	  return OS_ERROR;
   }

   return OS_SUCCESS;
} /* end OS_SocketAddrFromString_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_SocketAddrGetPort_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SocketAddrGetPort_Impl(uint16 *PortNum, const OS_SockAddr_t *Addr)
{
   uint16 sa_port;
   const OS_freertos_sockaddr_Accessor_t *Accessor;

   Accessor = (const OS_freertos_sockaddr_Accessor_t *)Addr->AddrData;

   switch(Accessor->freertos_sockaddr.sin_family)
   {
   case FREERTOS_AF_INET:
      sa_port = Accessor->freertos_sockaddr.sin_port;
      break;
   default:
      return OS_ERR_BAD_ADDRESS;
      break;
   }

   *PortNum = FreeRTOS_ntohs(sa_port);

   return OS_SUCCESS;
} /* end OS_SocketAddrGetPort_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_SocketAddrSetPort_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See description in os-impl.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_SocketAddrSetPort_Impl(OS_SockAddr_t *Addr, uint16 PortNum)
{
   uint16 sa_port;
   OS_freertos_sockaddr_Accessor_t *Accessor;

   sa_port = FreeRTOS_htons(PortNum);
   Accessor = (OS_freertos_sockaddr_Accessor_t *)Addr->AddrData;

   switch(Accessor->freertos_sockaddr.sin_family)
   {
   case FREERTOS_AF_INET:
      Accessor->freertos_sockaddr.sin_port = sa_port;
      break;
   default:
      return OS_ERR_BAD_ADDRESS;
   }

   return OS_SUCCESS;
} /* end OS_SocketAddrSetPort_Impl */

#endif
