#include <stdio.h>
#include "lasev.h"
#include <stdlib.h>
#include <signal.h>
//#include <vld.h>

#define BUF_SIZE 1024

static le_TcpConnection* client;
static int read_counter = 0;
static char buff[BUF_SIZE] = {0};
static int rbytes = 0;
static int sbytes = 0;

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
	printf("writeCB bytes: %d\n", bytes);
	free(req);
}

static void sendMsg(le_TcpConnection* client, const char* text, int bytes) {
	int result;
	le_Buffer sendbuf;
	le_WriteReq* req;

	sendbuf.base = (char*)text;
	sendbuf.len = bytes;
	req = (le_WriteReq*)malloc(sizeof(le_WriteReq));
	req->data = (void*)text;

	result = le_write(client, req, &sendbuf, 1, writeCB);
	if( result ) {
		errorLog(client->loop, "sendMsg");
		free(req);
	} else {
		printf("le_write success!\n");
	}
}

void readCB(le_TcpConnection* client, int bytes, char* buf) {
	if( bytes < 0 ) {
		errorLog(client->loop, "readCB");
		le_connectionClose(client);
		return;
	}
	
	rbytes += bytes;
	printf("readCB text is: (%d)%s, rbytes: %d, sbytes: %d\n", bytes, buf, rbytes, sbytes);

	if( read_counter++ >= 10 ) {
		le_connectionClose(client);
	}
}

void connectionCB(le_TcpConnection* client, int status) {
	int result;
	char* text = "hello world!";
	
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

	sendMsg(client, text, strlen(text) + 1);

	result = le_startRead(client, readCB, allocaCB);
	if( result ) {
		errorLog(client->loop, "le_startRead");
	} else {
		printf("le_startRead success!\n");
	}
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
		errorLog(loop, "le_connect");
	} else {
		printf("le_connect success!\n");
	}

	le_run(loop);

	le_eventLoopDelete(loop);
	
	printf("<---------client closed!--------->\n");

	getchar();
	return 0;
}
