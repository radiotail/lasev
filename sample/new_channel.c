#include <stdio.h>
#include "lasev.h"
#include <malloc.h>
#include <signal.h>
#include "le_safeQueue.h"
#include <vld.h>

typedef struct TestChannel 
{
	struct le_Channel channel;
	int num;
	int state;
} TestChannel;

#define CHANNEL_COUNT 10000000

static le_TcpServer* server;
static le_EventLoop* loop;
static le_Channel* quitChannel;
static TestChannel testChannels[CHANNEL_COUNT];
static le_time_t workTime;
static le_time_t testTime;

static void errorLog(le_EventLoop* loop, const char* title) {
	int err = le_getErrorCode(loop);
	printf("%s: error(%d)%s\n", title, err, le_strerror(err));
}

void readCB(le_TcpConnection* client, int bytes, char* buf) {

}

void connectionCB(le_TcpServer* server, int status) {

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

void sendChannelClose(le_Channel* channel) {
	TestChannel* test = (TestChannel*)channel;
	if( test->num == CHANNEL_COUNT - 1 ) {
		testTime = GetTickCount() - testTime;
		printf("testTime: %d\n", testTime);
	}
}

void sendChannelCB(le_Channel* channel, int status) {
	TestChannel* test = (TestChannel*)channel;
	if(test->state == 0) {
		le_channelClose(channel);
	}
}

static void* worker(void* arg) {
	int i, j;
	workTime = GetTickCount();
	
	//for(j = 0; j < 1000; j++) {
	//	for(i = 0; i < CHANNEL_COUNT; i++) {
	//		le_channelPost(&testChannels[i].channel);
	//	}
	//}
	//
	for(i = 0; i < CHANNEL_COUNT; i++) {
		testChannels[i].state = 0;
		le_channelPost(&testChannels[i].channel);
	}
	
	workTime = GetTickCount() - workTime;
	printf("worker time: %d\n", workTime);

	return NULL;
}

int main() {
	pthread_t thread;
	int result;
	int i;

	server = (le_TcpServer*)malloc(sizeof(le_TcpServer));
	quitChannel = (le_Channel*)malloc(sizeof(le_Channel));

	hookSignals();
	printf("<--------lasev started!-------->\n");

	loop = le_eventLoopCreate();

	le_tcpServerInit(loop, server, connectionCB, NULL);
	le_channelInit(loop, quitChannel, channelCB, channelClose);

	for(i = 0; i < CHANNEL_COUNT; i++) {
		testChannels[i].num = i;
		testChannels[i].state = 1;
		le_channelInit(loop, &testChannels[i].channel, sendChannelCB, sendChannelClose);
	}

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
	
	testTime = GetTickCount();

	le_pthreadCreate(&thread, worker, NULL);

	le_run(loop);

	le_eventLoopDelete(loop);

	le_pthreadJoin(thread);
	
	free(server);

	printf("<--------lasev closed!-------->\n");
	unhookSignals();

	getchar();
	return 0;
}
