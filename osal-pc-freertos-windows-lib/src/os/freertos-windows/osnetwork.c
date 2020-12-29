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

//const int OS_IMPL_SOCKET_FLAGS = O_NONBLOCK;

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
 * The socket table.
 *
 * This is shared by all OSAL entities that perform low-level I/O.
 */
OS_FreeRTOS_socket_entry_t OS_impl_socket_table[OS_MAX_NUM_OPEN_FILES];
//TODO: Figure out where to initialize this.

/****************************************************************************************
 INITIALIZATION FUNCTION
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
    int32 return_code;

    //TODO: gethostname not a thing in FreeRTOS
    if ( gethostname(host_name, name_len) < 0 )
    {
        return_code = OS_ERROR;
    }
    else
    {
        /*
         * posix does not say that the name is always
         * null terminated, so its worthwhile to ensure it
         */
        host_name[name_len - 1] = 0;
        return_code = OS_SUCCESS;
    }

    return(return_code);
} /* end OS_NetworkGetHostName_Impl */

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
	BaseType_t os_flags;

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

	OS_impl_socket_table[sock_id].socket = FreeRTOS_socket(os_domain, os_type, os_proto);
	if (OS_impl_socket_table[sock_id].socket == FREERTOS_INVALID_SOCKET)
	{
	   //Insufficient FreeRTOS heap memory
		OS_impl_socket_table[sock_id].socket = NULL;
		return OS_ERROR;
	}

	/*
	* Setting the FREERTOS_SO_REUSE_LISTEN_SOCKET flag helps during debugging when there might be frequent
	* code restarts.  However if setting the option fails then it is not worth bailing out over.
	*/
	//TODO: This may only work for TCP, but as the above commment mentions, it shouldn't be the end of the world
	os_flags = 1;
	FreeRTOS_setsockopt(OS_impl_socket_table[sock_id].socket, 0, FREERTOS_SO_REUSE_LISTEN_SOCKET, &os_flags, sizeof(os_flags));

	//TODO: I don't think this applies to FreeRTOS sockets but I really don't know
	//TODO: This may need more calls to setsockopt instead???
	//   /*
	//    * Set the standard options on the filehandle by default --
	//    * this may set it to non-blocking mode if the implementation supports it.
	//    * any blocking would be done explicitly via the select() wrappers
	//    */
	//   os_flags = fcntl(OS_impl_socket_table[sock_id].socket, F_GETFL);
	//   os_flags |= OS_IMPL_SOCKET_FLAGS;
	//   fcntl(OS_impl_socket_table[sock_id].socket, F_SETFL, os_flags);
	//
	//   OS_impl_socket_table[sock_id].selectable =
	//           ((os_flags & O_NONBLOCK) != 0);

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

   os_result = FreeRTOS_bind(OS_impl_socket_table[sock_id].socket, sa, addrlen);
   if (os_result < 0)
   {
      return OS_ERROR;
   }

   /* Start listening on the socket (implied for stream sockets) */
   if (OS_stream_table[sock_id].socket_type == OS_SocketType_STREAM)
   {
      os_result = FreeRTOS_listen(OS_impl_socket_table[sock_id].socket, 10);
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
//   int sockopt;
   socklen_t slen;
   uint32 operation;
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
       os_status = FreeRTOS_connect(OS_impl_socket_table[sock_id].socket, sa, slen);
       //TODO: Catch all of the possible error codes correctly
       if (os_status < 0)
       {
           if (errno != pdFREERTOS_ERRNO_EINPROGRESS) //Maybe need abs()
           {
               return_code = OS_ERROR;
           }
           else
           {
               operation = OS_STREAM_STATE_WRITABLE;
               if (OS_impl_socket_table[sock_id].selectable)
               {
                   return_code = OS_SelectSingle_Impl(sock_id, &operation, timeout);
               }
               if (return_code == OS_SUCCESS)
               {
                   if ((operation & OS_STREAM_STATE_WRITABLE) == 0)
                   {
                       return_code = OS_ERROR_TIMEOUT;
                   }
                   else
                   {
                	   //TODO: getsockopt not a thing in FreeRTOS
//                       sockopt = 0;
//                       slen = sizeof(sockopt);
//                       os_status = getsockopt(OS_impl_socket_table[sock_id].socket, SOL_SOCKET, SO_ERROR, &sockopt, &slen);
//                       if (os_status < 0 || sockopt != 0)
//                       {
//                           return_code = OS_ERROR;
//                       }
                   }
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
//   int os_flags;

   operation = OS_STREAM_STATE_READABLE;
   if (OS_impl_socket_table[sock_id].selectable)
   {
       return_code = OS_SelectSingle_Impl(sock_id, &operation, timeout);
   }
   else
   {
       return_code = OS_SUCCESS;
   }

   if (return_code == OS_SUCCESS)
   {
      if ((operation & OS_STREAM_STATE_READABLE) == 0)
      {
         return_code = OS_ERROR_TIMEOUT;
      }
      else
      {
         addrlen = Addr->ActualLength;
         OS_impl_socket_table[connsock_id].socket = FreeRTOS_accept(OS_impl_socket_table[sock_id].socket, (struct freertos_sockaddr *)Addr->AddrData, &addrlen);
         //TODO: Not certain about this. Could return either invalid socket or null
         if (OS_impl_socket_table[connsock_id].socket == NULL || OS_impl_socket_table[connsock_id].socket ==FREERTOS_INVALID_SOCKET )
         {
            return_code = OS_ERROR;
         }
         else
         {
             Addr->ActualLength = addrlen;

             //TODO: I don't think this applies to FreeRTOS sockets but I really don't know
			//TODO: This may need more calls to setsockopt instead???
//             /*
//              * Set the standard options on the filehandle by default --
//              * this may set it to non-blocking mode if the implementation supports it.
//              * any blocking would be done explicitly via the select() wrappers
//              */
//             os_flags = fcntl(OS_impl_socket_table[connsock_id].socket, F_GETFL);
//             os_flags |= OS_IMPL_SOCKET_FLAGS;
//             fcntl(OS_impl_socket_table[connsock_id].socket, F_SETFL, os_flags);
//
//             OS_impl_socket_table[connsock_id].selectable =
//                     ((os_flags & O_NONBLOCK) != 0);
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
      sa = (struct freertos_sockaddr *)RemoteAddr->AddrData;
   }

   operation = OS_STREAM_STATE_READABLE;
   /*
    * If "O_NONBLOCK" flag is set then use select()
    * Note this is the only way to get a correct timeout
    */
   if (OS_impl_socket_table[sock_id].selectable)
   {
       return_code = OS_SelectSingle_Impl(sock_id, &operation, timeout);
   }
   else
   {
       return_code = OS_SUCCESS;
   }

   if (return_code == OS_SUCCESS)
   {
      if ((operation & OS_STREAM_STATE_READABLE) == 0)
      {
         return_code = OS_ERROR_TIMEOUT;
      }
      else
      {
         os_result = FreeRTOS_recvfrom(OS_impl_socket_table[sock_id].socket, buffer, buflen, 0, sa, &addrlen);
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

   os_result = FreeRTOS_sendto(OS_impl_socket_table[sock_id].socket, buffer, buflen, 0, sa, addrlen);
   if (os_result < 0)
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
   const void *addrbuffer;
   const OS_freertos_sockaddr_Accessor_t *Accessor;

   Accessor = (const OS_freertos_sockaddr_Accessor_t *)Addr->AddrData;

   switch(Accessor->freertos_sockaddr.sin_family)
   {
   case FREERTOS_AF_INET:
      addrbuffer = &Accessor->freertos_sockaddr.sin_addr;
      break;
   default:
      return OS_ERR_BAD_ADDRESS;
      break;
   }

   if (FreeRTOS_inet_ntop(Accessor->freertos_sockaddr.sin_family, addrbuffer, buffer, buflen) == NULL)
   {
      return OS_ERROR;
   }

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
   void *addrbuffer;
   OS_freertos_sockaddr_Accessor_t *Accessor;

   Accessor = (OS_freertos_sockaddr_Accessor_t *)Addr->AddrData;

   switch(Accessor->freertos_sockaddr.sin_family)
   {
   case FREERTOS_AF_INET:
      addrbuffer = &Accessor->freertos_sockaddr.sin_addr;
      break;
   default:
      return OS_ERR_BAD_ADDRESS;
      break;
   }

   if (FreeRTOS_inet_pton(Accessor->freertos_sockaddr.sin_family, string, addrbuffer) < 0)
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
    /* FreeRTOS does not have the GetHostId call -
     * it is deprecated in other OS's anyway and not a good idea to use it
     */
    return OS_ERR_NOT_IMPLEMENTED;
} /* end OS_NetworkGetID_Impl */
#else  /* OS_INCLUDE_NETWORK */

/****************************************************************************************
 NOT IMPLEMENTED OPTION
 This block provides stubs in case this module is disabled by config
 ****************************************************************************************/

/*
 * The "no-network" block includes the required API calls
 * that all return OS_ERR_NOT_IMPLEMENTED
 */
#include "../portable/os-impl-no-network.c"

#endif

