#include <stdio.h>
#include "lasev.h"
#include <stdlib.h>
#include <signal.h>
//#include <vld.h>

#define BUF_SIZE 1024

static char buff[BUF_SIZE] = {0};
static char* msgText = NULL;
static int msgLen = 0;
static le_time_t timeLimit = 10000; //ms
static le_time_t startTime = 0;
static long long readMsgTimes = 0;
static long long readMsgtBytes = 0;


static void errorLog(le_EventLoop* loop, const char* title) {
	int err = le_getErrorCode(loop);
	printf("%s: error(%d)%s\n", title, err, le_strerror(err));
}

static void benchmarkResult() {
	printf("\n====================benchmark result====================\n");
	printf(" read messages bytes: %lld bytes\n", readMsgtBytes);
	printf(" read messages times: %lld \n", readMsgTimes);
	printf(" average messages size: %.3f bytes\n",(double)readMsgtBytes / readMsgTimes);
	printf(" throughtput: %.3f kb/s\n",(double)readMsgtBytes * 1000 / 1024 / timeLimit);
	printf("========================================================\n\n");
}

void allocaCB(le_TcpConnection* client, le_Buffer* buf) {
	buf->base = buff;
	buf->len = BUF_SIZE;
}

void closeCB(le_TcpConnection* client) {
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

	sendbuf.base = (char*)msgText;
	sendbuf.len  = msgLen;
	req = (le_WriteReq*)malloc(sizeof(le_WriteReq));

	result = le_write(client, req, &sendbuf, 1, writeCB);
	if( result ) {
		errorLog(client->loop, "sendMsg");
		free(req);
	}
}

void readCB(le_TcpConnection* client, int bytes, char* buf) {
	if( bytes <= 0 ) {
		le_connectionClose(client);
		return;
	}
	
	//printf("read cb: %p, %s\n", client, buf);
	readMsgTimes++;
	readMsgtBytes += bytes;

	if( le_getNowTime(client->loop) - startTime > timeLimit ) {
		le_connectionShutdown(client);
	} else {
		sendMsg(client);
	}
}

void connectionCB(le_TcpConnection* client, int status) {
	int result;
	
	if( status != LE_OK ) {
		errorLog(client->loop, "connectionCB");
		le_connectionClose(client);
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

	sendMsg(client);
}

int main(int argc, char **argv) {
	int i;
	int result;
	le_TcpConnection* client;
	le_EventLoop* loop;
	int clientCount = 100;

	msgLen = 64;
	msgText = malloc(msgLen);
	for( i = 0; i < msgLen; ++i) {
		msgText[i] = i % 26 + 65;
	}

	printf("client started!\n");
	printf("client count: %d\n", clientCount);
	printf("test time: %d\n", timeLimit);

	loop = le_eventLoopCreate();

	startTime = le_getNowTime(loop);

	for(i = 0; i < clientCount; ++i) {
		client = (le_TcpConnection*)malloc(sizeof(le_TcpConnection));
		le_tcpConnectionInit(loop, client, closeCB);

		result = le_connect(client, "127.0.0.1", 8611, connectionCB);
		if( result ) {
			errorLog(loop, "le_connect");
		}
	}

	le_run(loop);

	benchmarkResult();

	le_eventLoopDelete(loop);

	free(msgText);
	
	printf("client closed!\n");

	getchar();
	return 0;
}
