/*
** networktest.c
**
** This program is an OSAL sample that tests the OSAL network functions.
**
*/

#include <stdio.h>
#include <unistd.h>

#include "osapi.h"
#include "utassert.h"
#include "uttest.h"
#include "utbsp.h"

#define TASK_1_STACK_SIZE 1024
#define TASK_1_PRIORITY   100
#define TASK_2_STACK_SIZE 1024
#define TASK_2_PRIORITY   110
#define TASK_3_STACK_SIZE 1024
#define TASK_3_PRIORITY   100
#define TASK_4_STACK_SIZE 1024
#define TASK_4_PRIORITY   110

uint32 task_1_stack[TASK_1_STACK_SIZE];
uint32 task_1_id;
uint32 task_2_stack[TASK_2_STACK_SIZE];
uint32 task_2_id;
uint32 task_3_stack[TASK_3_STACK_SIZE];
uint32 task_3_id;
uint32 task_4_stack[TASK_4_STACK_SIZE];
uint32 task_4_id;

void NetworkTestTCPSetup(void);
void NetworkTestTCPCheck(void);

void NetworkTestUDPSetup(void);
void NetworkTestUDPCheck(void);

void Test_OS_SocketAddr(void);
void Test_OS_SocketOpen(void);
void Test_OS_SocketBind(void);
void Test_OS_SelectSingle(void);
void Test_OS_SelectMultiple(void);
void Test_OS_NetworkGetHostName (void);
void Test_OS_NetworkGetID (void);

uint32 clientTCP_id;
uint32 serverTCP_id;
uint32 clientUDP_id;
uint32 serverUDP_id;

int counterUDP = 0;
int fullCountUDP = 0;
int sentCountUDP = 0;
int recvCountUDP = 0;

int counterTCP = 0;

/* ********************** MAIN **************************** */

void client_task_TCP(void)
{
	OS_SockAddr_t client_Addr;
	OS_SockAddr_t xServer;
	int32	expected = OS_SUCCESS;
	int32 	actual;

    OS_printf("Starting client task\n");

    OS_TaskRegister();


    //
	//Client socket
	//
	actual = OS_SocketOpen(&clientTCP_id, OS_SocketDomain_INET, OS_SocketType_STREAM);
	UtAssert_True(actual == expected, "OS_SocketOpen() (%ld) == OS_SUCCESS", (long)actual);
	UtAssert_True(clientTCP_id != 0, "clientTCP_id (%lu) != 0", (unsigned long)clientTCP_id);

	actual = OS_SocketAddrInit(&client_Addr, OS_SocketDomain_INET);
	UtAssert_True(actual == expected, "OS_SocketAddrInit() (%ld) == OS_SUCCESS", (long)actual);

//	actual = OS_SocketAddrFromString(&client_Addr, "192.168.0.4");
//	UtAssert_True(actual == expected, "OS_SocketAddrFromString() (%ld) == OS_SUCCESS", (long)actual);

//	uint8_t addrString[ 50 ];
//	memset( addrString, 0x00, sizeof( addrString ) );
//	actual = OS_SocketAddrToString((char *) addrString, sizeof( addrString ), &client_Addr);
//	UtAssert_True(actual == expected, "OS_SocketAddrToString() (%ld) == OS_SUCCESS", (long)actual);

	actual = OS_SocketAddrSetPort(&client_Addr, 5006UL);
	UtAssert_True(actual == expected, "OS_SocketAddrSetPort() (%ld) == OS_SUCCESS", (long)actual);

	actual = OS_SocketBind(clientTCP_id, &client_Addr);
	UtAssert_True(actual == expected, "OS_SocketBind() (%ld) == OS_SUCCESS", (long)actual);

    OS_printf("Delay for 1 second before starting\n");
    OS_TaskDelay(2000);
	actual = OS_SocketConnect(clientTCP_id, &client_Addr, OS_PEND);
	UtAssert_True(actual == expected, "OS_SocketConnect() (%ld) == OS_SUCCESS", (long)actual);
}

void server_task_TCP(void)
{
	OS_SockAddr_t server_Addr;
	OS_SockAddr_t xClient;
	int32	expected = OS_SUCCESS;
	int32 	actual;

    OS_printf("Starting server task\n");

    OS_TaskRegister();


    //
	//Server socket
	//
	actual = OS_SocketOpen(&serverTCP_id, OS_SocketDomain_INET, OS_SocketType_STREAM);
	UtAssert_True(actual == expected, "OS_SocketOpen() (%ld) == OS_SUCCESS", (long)actual);
	UtAssert_True(serverTCP_id != 0, "serverTCP_id (%lu) != 0", (unsigned long)serverTCP_id);

	actual = OS_SocketAddrInit(&server_Addr, OS_SocketDomain_INET);
	UtAssert_True(actual == expected, "OS_SocketAddrInit() (%ld) == OS_SUCCESS", (long)actual);

	actual = OS_SocketBind(serverTCP_id, &server_Addr);
	UtAssert_True(actual == expected, "OS_SocketBind() (%ld) == OS_SUCCESS", (long)actual);

	int32_t lBytes;
	uint8_t cReceivedString[ 60 ];

    OS_printf("Delay for 1 second before starting\n");
    OS_TaskDelay(1000);

	actual = OS_SocketAccept(serverTCP_id, NULL, NULL, 0);
	UtAssert_True(actual == OS_INVALID_POINTER, "OS_SocketAccept(NULL) (%ld) == OS_INVALID_POINTER", (long)actual);

	actual = OS_SocketAccept(serverTCP_id, &clientTCP_id, &xClient, OS_PEND);
	UtAssert_True(actual == expected, "OS_SocketAccept() (%ld) == OS_SUCCESS", (long)actual);
}

void client_task_UDP(void)
{
	OS_SockAddr_t client_Addr;
	int32 	expected = OS_SUCCESS;
	int32 	actual;

    OS_printf("Starting client task\n");
    OS_TaskRegister();

    //
	//Client socket
	//
	actual = OS_SocketOpen(&clientUDP_id, OS_SocketDomain_INET, OS_SocketType_DATAGRAM);
	UtAssert_True(actual == expected, "OS_SocketOpen() (%ld) == OS_SUCCESS", (long)actual);
	UtAssert_True(clientUDP_id != 0, "clientUDP_id (%lu) != 0", (unsigned long)clientUDP_id);

	actual = OS_SocketAddrInit(&client_Addr, OS_SocketDomain_INET);
	UtAssert_True(actual == expected, "OS_SocketAddrInit() (%ld) == OS_SUCCESS", (long)actual);

	uint8_t addrString[ 50 ];
	memset( addrString, 0x00, sizeof( addrString ) );
	actual = OS_SocketAddrToString((char *) addrString, sizeof( addrString ), &client_Addr);
	UtAssert_True(actual == expected, "OS_SocketAddrToString() (%ld) == OS_SUCCESS", (long)actual);

	actual = OS_SocketAddrFromString(&client_Addr, "192.168.0.4");
	UtAssert_True(actual == expected, "OS_SocketAddrFromString() (%ld) == OS_SUCCESS", (long)actual);

	memset( addrString, 0x00, sizeof( addrString ) );
	actual = OS_SocketAddrToString((char *) addrString, sizeof( addrString ), &client_Addr);
	UtAssert_True(actual == expected, "OS_SocketAddrToString() (%ld) == OS_SUCCESS", (long)actual);

	actual = OS_SocketAddrSetPort(&client_Addr, 5005UL);
	UtAssert_True(actual == expected, "OS_SocketAddrSetPort() (%ld) == OS_SUCCESS", (long)actual);

	uint8_t cString[ 50 ];
	uint32_t ulCount = 0UL;

    OS_printf("Delay for 1 second before starting\n");
    OS_TaskDelay(1000);


    for(int i=0; i<100; i++)
	{
		sprintf( ( char * ) cString, "Server received (not zero copy): Message number %lu\r\n", ulCount );
		expected = strlen( ( const char * ) cString );
		actual = OS_SocketSendTo(clientUDP_id, cString, strlen( ( const char * ) cString ), &client_Addr);
		if(actual == expected)
		{
			sentCountUDP++;
		}

		ulCount++;
		//Pause every once in a while to make sure the buffer doesn't get full
		if(sentCountUDP%10==0)
		{
			OS_TaskDelay(100);
		}
	}
}

void server_task_UDP(void)
{
	OS_SockAddr_t server_Addr;
	OS_SockAddr_t xClient;
	int32 	expected = OS_SUCCESS;
	int32 	actual;

    OS_printf("Starting server task\n");
    OS_TaskRegister();

    //
	//Server socket
	//
	actual = OS_SocketOpen(&serverUDP_id, OS_SocketDomain_INET, OS_SocketType_DATAGRAM);
	UtAssert_True(actual == expected, "OS_SocketOpen() (%ld) == OS_SUCCESS", (long)actual);
	UtAssert_True(serverUDP_id != 0, "socket_id (%lu) != 0", (unsigned long)serverUDP_id);

	actual = OS_SocketAddrInit(&server_Addr, OS_SocketDomain_INET);
	UtAssert_True(actual == expected, "OS_SocketAddrInit() (%ld) == OS_SUCCESS", (long)actual);

	uint8_t addrString[ 50 ];
	memset( addrString, 0x00, sizeof( addrString ) );
	actual = OS_SocketAddrToString((char *) addrString, sizeof( addrString ), &server_Addr);
	UtAssert_True(actual == expected, "OS_SocketAddrToString() (%ld) == OS_SUCCESS", (long)actual);

	actual = OS_SocketAddrSetPort(&server_Addr, 5005UL);
	UtAssert_True(actual == expected, "OS_SocketAddrSetPort() (%ld) == OS_SUCCESS", (long)actual);

	actual = OS_SocketBind(serverUDP_id, NULL);
	UtAssert_True(actual == OS_INVALID_POINTER, "OS_SocketBind(NULL) (%ld) == OS_INVALID_POINTER", (long)actual);

	actual = OS_SocketBind(serverUDP_id, &server_Addr);
	UtAssert_True(actual == expected, "OS_SocketBind() (%ld) == OS_SUCCESS", (long)actual);

	int32_t lBytes;
	uint8_t cReceivedString[ 60 ];

    OS_printf("Delay for 1 second before starting\n");
    OS_TaskDelay(1000);

	while(true)
	{
		memset( cReceivedString, 0x00, sizeof( cReceivedString ) );
		lBytes = OS_SocketRecvFrom(serverUDP_id, cReceivedString, sizeof( cReceivedString ), &xClient, OS_PEND);
		if(lBytes == strlen( ( const char * ) cReceivedString))
		{
			recvCountUDP++;
		}

		counterUDP++;
	}
}

void OS_Application_Startup(void)
{
	if (OS_API_Init() != OS_SUCCESS)
	{
		UtAssert_Abort("OS_API_Init() failed");
	}


	UtTest_Add(NetworkTestTCPCheck, NetworkTestTCPSetup, NULL, "NetworkTest - TCP");
	UtTest_Add(NetworkTestUDPCheck, NetworkTestUDPSetup, NULL, "NetworkTest - UDP");
	UtTest_Add(Test_OS_SocketAddr, NULL, NULL, "Test_OS_SocketAddr");
	UtTest_Add(Test_OS_SocketOpen, NULL, NULL, "Test_OS_SocketOpen");
	UtTest_Add(Test_OS_SocketBind, NULL, NULL, "Test_OS_SocketBind");
	UtTest_Add(Test_OS_SelectSingle, NULL, NULL, "Test_OS_SelectSingle");
	UtTest_Add(Test_OS_SelectMultiple, NULL, NULL, "Test_OS_SelectMultiple");
	UtTest_Add(Test_OS_NetworkGetHostName, NULL, NULL, "Test_OS_NetworkGetHostName");
	UtTest_Add(Test_OS_NetworkGetID, NULL, NULL, "Test_OS_NetworkGetID");



}

void NetworkTestTCPSetup(void)
{
	uint32	status;

	//Need to delay to allow the IP tasks to kick in
	OS_TaskDelay(1000);

	/*
	** Create the client task.
	*/
	status = OS_TaskCreate( &task_1_id, "Task client", client_task_TCP, task_1_stack, TASK_1_STACK_SIZE, TASK_1_PRIORITY, 0);
	UtAssert_True(status == OS_SUCCESS, "Task client create Id=%u Rc=%d", (unsigned int)task_1_id, (int)status);


	/*
	** Create the server task.
	*/
	status = OS_TaskCreate( &task_2_id, "Task server", server_task_TCP, task_2_stack, TASK_2_STACK_SIZE, TASK_2_PRIORITY, 0);
	UtAssert_True(status == OS_SUCCESS, "Task server create Id=%u Rc=%d", (unsigned int)task_2_id, (int)status);

	/*
	* Time limited execution
	*/
//	while (counterTCP < 100)
//	{
//	  OS_TaskDelay(100);
//	}
   OS_TaskDelay(3000);
}

void NetworkTestUDPSetup(void)
{
	uint32	status;

	/*
	** Create the client task.
	*/
	status = OS_TaskCreate( &task_3_id, "Task client", client_task_UDP, task_3_stack, TASK_3_STACK_SIZE, TASK_3_PRIORITY, 0);
	UtAssert_True(status == OS_SUCCESS, "Task client create Id=%u Rc=%d", (unsigned int)task_3_id, (int)status);

	/*
	** Create the server task.
	*/
	status = OS_TaskCreate( &task_4_id, "Task server", server_task_UDP, task_4_stack, TASK_4_STACK_SIZE, TASK_4_PRIORITY, 0);
	UtAssert_True(status == OS_SUCCESS, "Task server create Id=%u Rc=%d", (unsigned int)task_4_id, (int)status);

	/*
	* Time limited execution
	*/
   while (counterUDP < 100)
   {
	  OS_TaskDelay(100);
   }
}

void NetworkTestTCPCheck(void)
{

}

void NetworkTestUDPCheck(void)
{
	UtAssert_True(sentCountUDP == recvCountUDP, "The same number of messages sent (%d) were received (%d)", sentCountUDP, recvCountUDP);
	UtAssert_True(fullCountUDP == 0, "fullCountUDP (%d) should be 0", fullCountUDP);
}

void Test_OS_SocketAddr (void)
{
    /*
     * Test Case For:
     * int32 OS_SocketAddrInit(OS_SockAddr_t *Addr, OS_SocketDomain_t Domain)
     * int32 OS_SocketAddrToString(char *buffer, uint32 buflen, const OS_SockAddr_t *Addr)
     * int32 OS_SocketAddrSetPort(OS_SockAddr_t *Addr, uint16 PortNum)
     * int32 OS_SocketAddrGetPort(uint16 *PortNum, const OS_SockAddr_t *Addr)
     */
    OS_SockAddr_t Addr;
    char Buffer[32];
    uint16 PortNum;
    int32 expected = OS_SUCCESS;
    int32 actual = ~OS_SUCCESS;

    /* First verify nominal case for each function */
    actual = OS_SocketAddrInit(&Addr, OS_SocketDomain_INET);
    UtAssert_True(actual == expected, "OS_SocketAddrInit() (%ld) == OS_SUCCESS", (long)actual);

    actual = OS_SocketAddrToString(Buffer, sizeof(Buffer), &Addr);
    UtAssert_True(actual == expected, "OS_SocketAddrToString() (%ld) %s == OS_SUCCESS", (long)actual, Buffer);

    actual = OS_SocketAddrFromString(&Addr, "192.168.0.4");
    UtAssert_True(actual == expected, "OS_SocketAddrFromString() (%ld) == OS_SUCCESS", (long)actual);

    actual = OS_SocketAddrSetPort(&Addr, 1234);
    UtAssert_True(actual == expected, "OS_SocketAddrSetPort() (%ld) == OS_SUCCESS", (long)actual);

    actual = OS_SocketAddrGetPort(&PortNum, &Addr);
    UtAssert_True(actual == expected, "OS_SocketAddrGetPort() (%ld) == OS_SUCCESS", (long)actual);


    /* Verify invalid pointer checking in each function */
    expected = OS_INVALID_POINTER;

    actual = OS_SocketAddrInit(NULL, OS_SocketDomain_INET);
    UtAssert_True(actual == expected, "OS_SocketAddrInit() (%ld) == OS_INVALID_POINTER", (long)actual);

    actual = OS_SocketAddrToString(NULL, 0, NULL);
    UtAssert_True(actual == expected, "OS_SocketAddrToString() (%ld) == OS_INVALID_POINTER", (long)actual);

    actual = OS_SocketAddrFromString(NULL, NULL);
    UtAssert_True(actual == expected, "OS_SocketAddrFromString() (%ld) == OS_INVALID_POINTER", (long)actual);

    actual = OS_SocketAddrSetPort(NULL, 1234);
    UtAssert_True(actual == expected, "OS_SocketAddrSetPort() (%ld) == OS_INVALID_POINTER", (long)actual);

    actual = OS_SocketAddrGetPort(NULL, NULL);
    UtAssert_True(actual == expected, "OS_SocketAddrGetPort() (%ld) == OS_INVALID_POINTER", (long)actual);
}

/*****************************************************************************
 *
 * Test case for OS_SocketOpen()
 *
 *****************************************************************************/
void Test_OS_SocketOpen(void)
{
    /*
     * Test Case For:
     * int32 OS_SocketOpen(uint32 *sock_id, OS_SocketDomain_t Domain, OS_SocketType_t Type)
     */
    int32 expected = OS_SUCCESS;
    uint32 objid = 0xFFFFFFFF;
    int32 actual = OS_SocketOpen(&objid, OS_SocketDomain_INET, OS_SocketType_STREAM);
    UtAssert_True(actual == expected, "OS_SocketOpen() (%ld) == OS_SUCCESS", (long)actual);
    UtAssert_True(objid != 0, "objid (%lu) != 0", (unsigned long)objid);

    expected = OS_INVALID_POINTER;
    actual = OS_SocketOpen(NULL, OS_SocketDomain_INVALID, OS_SocketType_INVALID);
    UtAssert_True(actual == expected, "OS_SocketOpen(NULL) (%ld) == OS_INVALID_POINTER", (long)actual);

}

/*****************************************************************************
 *
 * Test case for OS_SocketBind()
 *
 *****************************************************************************/
void Test_OS_SocketBind(void)
{
    /*
     * Test Case For:
     * int32 OS_SocketBind(uint32 sock_id, const OS_SockAddr_t *Addr)
     */
	uint32 id;
    int32 expected = OS_SUCCESS;
    int32 actual = ~OS_SUCCESS;
    OS_SockAddr_t Addr;

    OS_SocketOpen(&id, OS_SocketDomain_INET, OS_SocketType_STREAM);
	OS_SocketAddrInit(&Addr, OS_SocketDomain_INET);

    actual = OS_SocketBind(id, &Addr);
    UtAssert_True(actual == expected, "OS_SocketBind() (%ld) == OS_SUCCESS", (long)actual);

    expected = OS_INVALID_POINTER;
    actual = OS_SocketBind(1, NULL);
    UtAssert_True(actual == expected, "OS_SocketBind(NULL) (%ld) == OS_INVALID_POINTER", (long)actual);

}

void Test_OS_SelectSingle(void)
{
    /*
     * Test Case For:
     * int32 OS_SelectSingle(uint32 objid, uint32 *StateFlags, int32 msecs);
     */
    int32 expected = OS_SUCCESS;
    uint32 StateFlags = 0;
    uint32 id;

    OS_SocketOpen(&id, OS_SocketDomain_INET, OS_SocketType_STREAM);

    int32 actual = OS_SelectSingle(id, &StateFlags, 0);

    /* Verify Outputs */
    UtAssert_True(actual == expected, "OS_SelectSingle() (%ld) == OS_SUCCESS", (long)actual);
}

void Test_OS_SelectMultiple(void)
{
    /*
     * Test Case For:
     * int32 OS_SelectMultiple(OS_FdSet *ReadSet, OS_FdSet *WriteSet, int32 msecs);
     */
    OS_FdSet ReadSet;
    OS_FdSet WriteSet;
    int32 expected = OS_SUCCESS;
    int32 actual;

    OS_SelectFdZero(&ReadSet);
    OS_SelectFdZero(&WriteSet);
    actual = OS_SelectMultiple(&ReadSet, &WriteSet, 0);

    /* Verify Outputs */
    UtAssert_True(actual == expected, "OS_SelectMultiple() (%ld) == OS_SUCCESS", (long)actual);
}

void Test_OS_NetworkGetHostName (void)
{
    /*
     * Test Case For:
     * int32 OS_NetworkGetHostName(char *host_name, uint32 name_len)
     */
    char Buffer[32];
    int32 expected = OS_SUCCESS;
    int32 actual = ~OS_SUCCESS;

    actual = OS_NetworkGetHostName(Buffer, sizeof(Buffer));
    UtAssert_True(actual == expected, "OS_NetworkGetHostName() (%ld) == OS_SUCCESS", (long)actual);

    expected = OS_INVALID_POINTER;
    actual = OS_NetworkGetHostName(NULL, sizeof(Buffer));
    UtAssert_True(actual == expected, "OS_NetworkGetHostName(Ptr=NULL) (%ld) == OS_INVALID_POINTER", (long)actual);

    expected = OS_ERROR;
    actual = OS_NetworkGetHostName(Buffer, 0);
    UtAssert_True(actual == expected, "OS_NetworkGetHostName(Size=0) (%ld) == OS_ERROR", (long)actual);
}

void Test_OS_NetworkGetID (void)
{
    /*
     * Test Case For:
     * int32 OS_NetworkGetID(void)
     */
    int32 expected;
    int32 actual;

    expected = 42;
    actual = OS_NetworkGetID();
    UtAssert_True(actual == expected, "OS_NetworkGetID(nominal) (%ld) == 42", (long)actual);
}
