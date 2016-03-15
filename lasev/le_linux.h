#ifndef LE_LINUX_H_
#define LE_LINUX_H_

#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/eventfd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#define LE_DEFAULT_EVENTS 1024

#define LE_EXTERN extern

struct le_TcpBasicEvent;
typedef void (*le_processCB)(struct le_TcpBasicEvent* event, unsigned int events);

typedef struct le_Buffer
{
	char* base;
	size_t len;
} le_Buffer;

// le_TcpBasicEvent is a base class of le_TcpServer and le_TcpConnection
typedef struct le_TcpBasicEvent
{
	int fd;
	int mask;
	le_processCB processCB;
} le_TcpBasicEvent;

#define LE_BASE_REQ_MEMBERS

#define LE_PLATFORM_LOOP_FIELDS    \
	int epollFd;				   \
	unsigned maxEventCount;		   \
	le_TcpBasicEvent channelEvent; \
	struct epoll_event* events;

#define LE_PLATFORM_WRITE_FIELDS  \
	le_Queue writeReqNode;		  \
	unsigned bufIndex;			  \
	unsigned bufCount;			  \
	size_t   totalSize;			  \
	le_Buffer* bufs;			  \
	le_Buffer bufsml[4];

#define LE_PLATFORM_SERVER_FIELDS  \
	le_TcpBasicEvent basicEvent;   \
	int pendingAcceptFd;

#define LE_PLATFORM_CONNECTION_FIELDS   \
	le_TcpBasicEvent basicEvent;		\
	size_t writeTotalSize;				\
	le_Queue writeReqHead;

#define LE_CONNECTION_PRIVATE_FIELDS \
	struct {						 \
		le_readCB readCB;			 \
		le_allocCB allocCB;			 \
	};

#define LE_CONNECTOR_PRIVATE_FIELDS \
	struct {						\
		le_ConnectReq connectReq;	\
		le_connectCB connectCB;		\
	};

struct le_EventLoop;

int le__setTcpNoDelay(struct le_EventLoop* loop, int socket, int enable);
int le__setTcpKeepAlive(struct le_EventLoop* loop, int socket, int enable);
int le__setTcpSendBuffer(struct le_EventLoop* loop, int socket, int size);

#define le_setPlatformTcpNoDelay(tcpEvent, enable) \
	le__setTcpNoDelay(tcpEvent->loop, tcpEvent->basicEvent.fd, enable)

#define le_setPlatformTcpKeepAlive(tcpEvent, enable) \
	le__setTcpKeepAlive(tcpEvent->loop, tcpEvent->basicEvent.fd, enable)

#define le_setPlatformTcpSendBuffer(tcpEvent, size) \
	le__setTcpSendBuffer(tcpEvent->loop, tcpEvent->basicEvent.fd, size)

static inline void Sleep(unsigned int ms) {
	struct timespec t;
	t.tv_sec = ms / 1000;
	t.tv_nsec = (ms % 1000) * 1000000;
	nanosleep(&t, NULL);
}

#endif

