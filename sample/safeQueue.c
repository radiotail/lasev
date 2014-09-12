#include <stdio.h>
#include "lasev.h"
#include <malloc.h>
#include <signal.h>
#include "le_safeQueue.h"
#include <vld.h>

typedef struct Test 
{
	le_Queue node;
	int a;
} Test;

#define TEXT_LEN 64
static char text[TEXT_LEN] = {0};
static le_TcpServer* server;
static le_EventLoop* loop;
static le_Channel* quitChannel;
static le_Channel sendChannel;
static le_safeQueueHead shead;

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
	free(req);
}

static void sendMsg(le_TcpConnection* client, const char* text, int bytes) {
	int result;
	le_Buffer sendbuf;
	le_WriteReq* req;

	sendbuf.base = (char*)text;
	sendbuf.len = bytes;
	req = (le_WriteReq*)malloc(sizeof(le_WriteReq));

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
	//printf("readCB: %d\n", bytes);

	sendMsg(client, buf, bytes);
}

void sendChannelCB(le_Channel* channel, int status) {
	le_Queue head;
	le_Queue* node;
	Test* p;

	printf("into sendChannelCB!\n");

	le_safeQueueSwap(&shead, &head);

	node = le_queueNext(&head);
	while( node != &head ) {
		p = (Test*)node;
		node = le_queueNext(node);

		printf("sendChannelCB: %d, node: %p, head: %p, prev: %p, next: %p\n", p->a, node, &head, node->prev, node->next);
		free(p);
	}

	printf("outo sendChannelCB!\n");
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
	le_channelClose(&sendChannel);
	le_channelClose(channel);
	le_serverClose(server);
}

void onSignal(int s) {
	switch(s)
	{
	case SIGINT:
	case SIGTERM:
		le_channelPost(quitChannel);
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

static void* worker(void* arg) {
	int i;
	
	for(i = 0; i < 10; i++) {
		Test* t = malloc(sizeof(Test));
		t->a = i;
		le_safeQueueAdd(&shead, &t->node);
		printf("worker: %d, node: %p, head: %p, prev: %p, next: %p\n", t->a, &t->node, &shead.queue, (&t->node)->prev, (&t->node)->next);
		le_channelPost(&sendChannel);
	}

	for(i = 10; i < 20; i++) {
		Test* t = malloc(sizeof(Test));
		t->a = i;
		le_safeQueueAdd(&shead, &t->node);
		printf("worker: %d, node: %p, head: %p, prev: %p, next: %p\n", t->a, &t->node, &shead.queue, (&t->node)->prev, (&t->node)->next);
		le_channelPost(&sendChannel);
	}
	
	return NULL;
}

int main() {
	pthread_t thread;
	int result;

	server = (le_TcpServer*)malloc(sizeof(le_TcpServer));
	quitChannel = (le_Channel*)malloc(sizeof(le_Channel));

	hookSignals();
	printf("<--------lasev started!-------->\n");

	loop = le_eventLoopCreate();

	le_tcpServerInit(loop, server, connectionCB, serverClose);
	le_channelInit(loop, quitChannel, channelCB, channelClose);
	le_channelInit(loop, &sendChannel, sendChannelCB, NULL);

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

	le_safeQueueInit(&shead);

	le_pthreadCreate(&thread, worker, NULL);

	le_run(loop);

	le_eventLoopDelete(loop);

	le_pthreadJoin(thread);

	le_safeQueueDestroy(&shead);

	printf("<--------lasev closed!-------->\n");
	unhookSignals();

	getchar();
	return 0;
}
