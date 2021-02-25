/*
 * network_defs.c
 *
 *  Created on: Jan 15, 2021
 *      Author: Christopher Sullivan, based on work by Jonathan Brandenburg
 */
#include <stdio.h>
#include <time.h>

#include "os-FreeRTOS.h"
#include <FreeRTOS.h>
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "freertos_logging.h"
#include "common_types.h"

/* Define a name that will be used for LLMNR and NBNS searches. */
#define mainHOST_NAME				"OSAL_main"
#define mainDEVICE_NICK_NAME		"windows_OSAL_main"

void Run_Test(void);

/****************************************************************************************
 INTERNAL FUNCTION
 ***************************************************************************************/

void prvMiscInitialisation( void );

/*
 * Just seeds the simple pseudo random number generator.
 */
static void prvSRand(UBaseType_t ulSeed);

/* The default IP and MAC address used by the demo.  The address configuration
 defined here will be used if ipconfigUSE_DHCP is 0, or if ipconfigUSE_DHCP is
 1 but a DHCP server could not be contacted.  See the online documentation for
 more information. */
const uint8_t ucIPAddress[4] = { configIP_ADDR0, configIP_ADDR1, configIP_ADDR2,
		configIP_ADDR3 };
const uint8_t ucNetMask[4] = { configNET_MASK0, configNET_MASK1,
		configNET_MASK2, configNET_MASK3 };
const uint8_t ucGatewayAddress[4] = { configGATEWAY_ADDR0, configGATEWAY_ADDR1,
		configGATEWAY_ADDR2, configGATEWAY_ADDR3 };
const uint8_t ucDNSServerAddress[4] = { configDNS_SERVER_ADDR0,
		configDNS_SERVER_ADDR1, configDNS_SERVER_ADDR2, configDNS_SERVER_ADDR3 };

/* Set the following constant to pdTRUE to log using the method indicated by the
 name of the constant, or pdFALSE to not log using the method indicated by the
 name of the constant.  Options include to standard out (xLogToStdout), to a disk
 file (xLogToFile), and to a UDP port (xLogToUDP).  If xLogToUDP is set to pdTRUE
 then UDP messages are sent to the IP address configured as the echo server
 address (see the configECHO_SERVER_ADDR0 definitions in FreeRTOSConfig.h) and
 the port number set by configPRINT_PORT in FreeRTOSConfig.h. */
const BaseType_t xLogToStdout = pdTRUE, xLogToFile = pdFALSE, xLogToUDP =
		pdFALSE;
/* Default MAC address configuration.  The demo creates a virtual network
 connection that uses this MAC address by accessing the raw Ethernet data
 to and from a real network connection on the host PC.  See the
 configNETWORK_INTERFACE_TO_USE definition for information on how to configure
 the real network connection to use. */
const uint8_t ucMACAddress[6] = { configMAC_ADDR0, configMAC_ADDR1,
		configMAC_ADDR2, configMAC_ADDR3, configMAC_ADDR4, configMAC_ADDR5 };


/* Use by the pseudo random number generator. */
static UBaseType_t ulNextRand;

/* Called by FreeRTOS+TCP when the network connects or disconnects.  Disconnect
 events are only received if implemented in the MAC driver. */
void vApplicationIPNetworkEventHook(eIPCallbackEvent_t eNetworkEvent) {
	uint32_t ulIPAddress, ulNetMask, ulGatewayAddress, ulDNSServerAddress;
	char cBuffer[16];
	static BaseType_t xTasksAlreadyCreated = pdFALSE;

	/* If the network has just come up...*/
	if (eNetworkEvent == eNetworkUp) {
		/* Print out the network configuration, which may have come from a DHCP
		 server. */
		FreeRTOS_GetAddressConfiguration(&ulIPAddress, &ulNetMask,
				&ulGatewayAddress, &ulDNSServerAddress);
		FreeRTOS_inet_ntoa(ulIPAddress, cBuffer);
		FreeRTOS_printf(( "\r\n\r\nIP Address: %s\r\n", cBuffer ));

		FreeRTOS_inet_ntoa(ulNetMask, cBuffer);
		FreeRTOS_printf(( "Subnet Mask: %s\r\n", cBuffer ));

		FreeRTOS_inet_ntoa(ulGatewayAddress, cBuffer);
		FreeRTOS_printf(( "Gateway Address: %s\r\n", cBuffer ));

		FreeRTOS_inet_ntoa(ulDNSServerAddress, cBuffer);
		FreeRTOS_printf(( "DNS Server Address: %s\r\n\r\n\r\n", cBuffer ));

		/* Create the tasks that use the IP stack if they have not already been
		 created. */
		if (xTasksAlreadyCreated == pdFALSE)
		{
			/* Create any tasks that depend on the network */
			uint32 main_task;
			BaseType_t status;
			status =  OS_TaskCreate(&main_task, "Main Test Task", Run_Test, NULL, 4096, 31, 0);

			if (status != OS_SUCCESS) {
				fprintf(stderr, "ERROR: Could not spawn main task\n");
			}
			else {
				xTasksAlreadyCreated = pdTRUE;
			}
		}
	}
}

UBaseType_t uxRand( void )
{
	const uint32_t ulMultiplier = 0x015a4e35UL, ulIncrement = 1UL;

	/* Utility function to generate a pseudo random number. */
	ulNextRand = ( ulMultiplier * ulNextRand ) + ulIncrement;
	return( ( int ) ( ulNextRand >> 16UL ) & 0x7fffUL );
}

#if( ipconfigUSE_LLMNR != 0 ) || ( ipconfigUSE_NBNS != 0 ) || ( ipconfigDHCP_REGISTER_HOSTNAME == 1 )
	const char *pcApplicationHostnameHook( void )
	{
		/* Assign the name "FreeRTOS" to this network node.  This function will
		be called during the DHCP: the machine will be registered with an IP
		address plus this name. */
		return mainHOST_NAME;
	}
#endif

#if( ipconfigUSE_LLMNR != 0 ) || ( ipconfigUSE_NBNS != 0 )
	BaseType_t xApplicationDNSQueryHook( const char *pcName )
	{
		BaseType_t xReturn;

		/* Determine if a name lookup is for this node.  Two names are given
		to this node: that returned by pcApplicationHostnameHook() and that set
		by mainDEVICE_NICK_NAME. */
		if( _stricmp( pcName, pcApplicationHostnameHook() ) == 0 )
		{
			xReturn = pdPASS;
		}
		else if( _stricmp( pcName, mainDEVICE_NICK_NAME ) == 0 )
		{
			xReturn = pdPASS;
		}
		else
		{
			xReturn = pdFAIL;
		}

		return xReturn;
	}
#endif

void prvMiscInitialisation(void)
{
	time_t xTimeNow;
	uint32_t ulLoggingIPAddress;

	ulLoggingIPAddress = FreeRTOS_inet_addr_quick(configECHO_SERVER_ADDR0,
			configECHO_SERVER_ADDR1, configECHO_SERVER_ADDR2,
			configECHO_SERVER_ADDR3);
	vLoggingInit(xLogToStdout, xLogToFile, xLogToUDP, ulLoggingIPAddress,
			configPRINT_PORT);

	/* Seed the random number generator. */
	time(&xTimeNow);
	FreeRTOS_debug_printf(( "Seed for randomiser: %lu\n", xTimeNow ));
	prvSRand((uint32_t) xTimeNow);
	FreeRTOS_debug_printf(
			( "Random numbers: %08X %08X %08X %08X\n", ipconfigRAND32(), ipconfigRAND32(), ipconfigRAND32(), ipconfigRAND32() ));
}

static void prvSRand( UBaseType_t ulSeed )
{
	/* Utility function to seed the pseudo random number generator. */
	ulNextRand = ulSeed;
}

/*
 * Callback that provides the inputs necessary to generate a randomized TCP
 * Initial Sequence Number per RFC 6528.  THIS IS ONLY A DUMMY IMPLEMENTATION
 * THAT RETURNS A PSEUDO RANDOM NUMBER SO IS NOT INTENDED FOR USE IN PRODUCTION
 * SYSTEMS.
 */
extern uint32_t ulApplicationGetNextSequenceNumber(uint32_t ulSourceAddress, uint16_t usSourcePort, uint32_t ulDestinationAddress, uint16_t usDestinationPort)
{
	( void ) ulSourceAddress;
	( void ) usSourcePort;
	( void ) ulDestinationAddress;
	( void ) usDestinationPort;

	return uxRand();
}
