//////////////////////////////////////////////////////////////////////////////////////
// Mail: radiotail86@gmail.com
// About the details of license, please read LICENSE
//////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include "lasev.h"
#include <stdlib.h>

#define BUF_SIZE 1024

static le_TcpConnection* client;
static le_Timer timer;
static int timer_counter = 0;
static char buff[BUF_SIZE] = {0};

static void errorLog(le_EventLoop* loop, const char* title) {
	int err = le_getErrorCode(loop);
	printf("%s: error(%d)%s\n", title, err, le_strerror(err));
}

void allocaCB(le_TcpConnection* client, le_Buffer* buf) {
	buf->base = buff;
	buf->len = BUF_SIZE;
}

void closeCB(le_TcpConnection* client) {
	printf("client close!!!\n");
	free(client);
}

void shutdownCB(le_TcpConnection* client) {
	printf("client shutdown!!!\n");
}

void writeCB(le_WriteReq* req, int bytes) {
	free(req);
}

static void sendMsg(le_TcpConnection* client) {
	int result;
	le_Buffer sendbuf;
	le_WriteReq* req;

	sprintf(buff, "%lu", (unsigned long)le_getNowTime(client->loop));
	printf("sendMsg: (%lu)%s\n", (long unsigned)strlen(buff) + 1, buff);

	sendbuf.base = (char*)buff;
	sendbuf.len = strlen(buff) + 1;
	req = (le_WriteReq*)malloc(sizeof(le_WriteReq));

	result = le_write(client, req, &sendbuf, 1, writeCB);
	if( result ) {
		errorLog(client->loop, "sendMsg");
		free(req);
	}
}

void readCB(le_TcpConnection* client, int bytes, char* buf) {
	if( bytes == LE_ERROR ) {
		errorLog(client->loop, "readCB");
		le_connectionClose(client);
		return;
	}

	printf("readCB: (%d)%s\n", bytes, buf);
}

void timerCB(le_Timer* timer) {
	printf("timer: %d\n", timer_counter);
	if( timer_counter++ < 10 ) {
		sendMsg(client);
	} else {
		le_timerClose(timer);
		le_connectionClose(client);
	}
	
}

void connectionCB(le_TcpConnection* client, int status) {
	int result;

	if( status == LE_OK ) {
		printf("get connection!!\n");
	} else {
		errorLog(client->loop, "connectionCB");
		return;
	}

	result = le_setTcpNoDelay(client, 1);
	if( result ) {
		errorLog(client->loop, "le_setTcpNoDelay");
	}

	result = le_startRead(client, readCB, allocaCB);
	if( result ) {
		errorLog(client->loop, "le_startRead");
	}

	le_timerInit(client->loop, &timer);
	le_timerStart(&timer, 1000, 5000, timerCB);
}

int main() {
	int result;
	le_EventLoop* loop;

	printf("<---------client started!--------->\n");

	loop = le_eventLoopCreate();
	client = (le_TcpConnection*)malloc(sizeof(le_TcpConnection));
	le_tcpConnectionInit(loop, client, closeCB);

	result = le_connect(client, "127.0.0.1", 8611, connectionCB);
	if( result ) {
		free(client);
		errorLog(loop, "le_connect");
	} else {
		printf("le_connect success!\n");
	}

	le_run(loop);

	le_eventLoopDelete(loop);

	printf("<---------client closed!--------->\n");;

	getchar();
	return 0;
}
