//////////////////////////////////////////////////////////////////////////////////////
// Mail: radiotail86@gmail.com
// About the details of license, please read LICENSE
//////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include "lasev.h"
#include <malloc.h>
#include <signal.h>
//#include <vld.h>

#define TEXT_LEN 1024
static char text[TEXT_LEN] = {0};
static le_TcpServer* server;
static le_EventLoop* loop;
static le_Channel* channel;
static int rbytes = 0;
static int sbytes = 0;

static void errorLog(le_EventLoop* loop, const char* title) {
	int err = le_getErrorCode(loop);
	printf("%s: error(%d)%s\n", title, err, le_strerror(err));
}

void allocaCB(le_TcpConnection* client, le_Buffer* buf) {
	buf->base = text;
	buf->len  = TEXT_LEN;
}

void serverClose(le_TcpServer* server) {
	printf("server close!!!\n");
	free(server);
}

void clientClose(le_TcpConnection* client) {
	free(client);
}

void shutdownCB(le_TcpConnection* client) {
	printf("client shutdown!!!\n");
}

void writeCB(le_WriteReq* req, int bytes) {
	//free(req->data);
	free(req);

	if (bytes == LE_ERROR) {
		int err = le_getErrorCode(loop);
		printf("write error: (%d)%s\n", err, le_strerror(err));
		return;
	}

	sbytes += bytes;
	printf("writeCB len: %d\n", bytes);
}

static void sendMsg(le_TcpConnection* client, const char* text, int bytes) {
	int result;
	le_Buffer sendbuf;
	le_WriteReq* req;

	/*sendbuf.base = (char*)malloc(65535);
	memset(sendbuf.base, 0, 65535);
	sendbuf.len = 65535;*/

	sendbuf.base = (char*)text;
	memset(sendbuf.base, 0, bytes);
	sendbuf.len = bytes;

	req = (le_WriteReq*)malloc(sizeof(le_WriteReq));
	req->data = (void*)sendbuf.base;
	
	result = le_write(client, req, &sendbuf, 1, writeCB);
	if( result ) {
		free(req);
		errorLog(client->loop, "le_write");
	}
}

void readCB(le_TcpConnection* client, int bytes, char* buf) {
	if( bytes < 0 ) {
		le_connectionClose(client);
		return;
	}
	printf("readCB text len: %d\n", bytes);
	rbytes += bytes;
	
	sendMsg(client, buf, bytes);
}

void connectionCB(le_TcpServer* server, int status) {
	int result;
	le_TcpConnection* client;

	if( status < 0 ) {
		errorLog(server->loop, "connectionCB");
		return;
	}
	
	client = (le_TcpConnection*)malloc(sizeof(le_TcpConnection));
	le_tcpConnectionInit(server->loop, client, clientClose);

	result = le_accept(server, client);
	if( result ) {
		errorLog(server->loop, "le_accept");
	}

	result = le_setTcpNoDelay(client, 1);
	if( result ) {
		errorLog(server->loop, "le_setTcpNoDelay");
	}
	
	//sendMsg(client, "", 0);
	result = le_startRead(client, readCB, allocaCB);
	if( result ) {
		errorLog(server->loop, "le_startRead");
	}
}

void channelClose(le_Channel* channel) {
	printf("channel close!\n");
	free(channel);
}

void channelCB(le_Channel* channel, int status) {
	le_channelClose(channel);
	le_serverClose(server);
}

void onSignal(int s) {
	switch(s)
	{
	case SIGINT:
	case SIGTERM:
		le_channelPost(channel);
		break;
	}

	signal(s, onSignal);
}

void hookSignals() {
	signal(SIGINT, onSignal);
	signal(SIGTERM, onSignal);
}

void unhookSignals() {
	signal(SIGINT, 0);
	signal(SIGTERM, 0);
}

int main() {
	int result;

	server = (le_TcpServer*)malloc(sizeof(le_TcpServer));
	channel = (le_Channel*)malloc(sizeof(le_Channel));

	hookSignals();
	printf("<--------lasev started!-------->\n");

	loop = le_eventLoopCreate();

	le_tcpServerInit(loop, server, connectionCB, serverClose);
	le_channelInit(loop, channel, channelCB, channelClose);

	result = le_bind(server, "0.0.0.0", 8611);
	if( result ) {
		errorLog(loop, "le_bind");
	} else {
		printf("le_bind success!\n");
	}

	result = le_listen(server, 511);
	if( result ) {
		errorLog(loop, "le_listen");
	} else {
		printf("le_listen success!\n");
	}

	le_run(loop);

	le_eventLoopDelete(loop);
	
	printf("rbytes: %d, sbytes: %d\n", rbytes, sbytes);

	printf("<--------lasev closed!-------->\n");
	unhookSignals();

	getchar();
	return 0;
}
