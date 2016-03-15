#include "lasev.h"
#include "le_internal.h"
#include "le_timerHeap.h"

#ifdef IOV_MAX
# define MAX_BUFS_COUNT IOV_MAX
#else
# define MAX_BUFS_COUNT 128
#endif

#define INVALID_SOCKET -1

static const uint64_t le_channelBytes = 1; //must be 8 bytes

const char* le_strerror(int err) {
	return (const char*)strerror(err);
}

int le_getErrorCode(le_EventLoop* loop) {
	return loop->errorCode;
}

static inline void le_updateNowTime(le_EventLoop* loop) {
#ifdef CLOCK_REALTIME_COARSE
# define WHICH_CLOCK CLOCK_REALTIME_COARSE
#else
# define WHICH_CLOCK CLOCK_MONOTONIC
#endif
	struct timespec ts;
	clock_gettime(WHICH_CLOCK, &ts);
	loop->time = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#undef WHICH_CLOCK
}

static inline int le_setNonBlocking(le_EventLoop* loop, int fd) {
	int flag = 1;

	if( ioctl(fd, FIONBIO, &flag) == -1 ) {
		le_setErrorCode(loop, errno);
		return LE_ERROR;
	}

	return LE_OK;
}

int le__setTcpNoDelay(le_EventLoop* loop, int fd, int enable) {
	if( fd == INVALID_SOCKET ) {
		le_setErrorCode(loop, EINVAL);
		return LE_ERROR;
	}

	if( setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&enable, sizeof(enable)) ) {
		le_setErrorCode(loop, errno);
		return LE_ERROR;
	}

	return LE_OK;
}

int le__setTcpKeepAlive(le_EventLoop* loop, int fd, int enable) {
	if( fd == INVALID_SOCKET ) {
		le_setErrorCode(loop, EINVAL);
		return LE_ERROR;
	}

	if( setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const char*)&enable, sizeof(enable)) ) {
		le_setErrorCode(loop, errno);
		return LE_ERROR;
	}

	return LE_OK;
}

int le__setTcpSendBuffer(le_EventLoop* loop, int fd, int size) {
	if( fd == INVALID_SOCKET ) {
		le_setErrorCode(loop, EINVAL);
		return LE_ERROR;
	}

	if( setsockopt(fd, SOL_SOCKET,SO_SNDBUF, (const char*)&size, sizeof(size)) ) {
		le_setErrorCode(loop, errno);
		return LE_ERROR;
	}

	return LE_OK;
}

static inline void le_connectionOver(le_TcpConnection* connection) {
	if( connection->masks & LE_CONNECTION ) {
		connection->masks &= ~LE_CONNECTION;
		le_queueRemove(&connection->connectionNode);
		LE_DECREASE_EVENTS(connection->loop);
	}

	if( connection->basicEvent.fd != INVALID_SOCKET ) {
		close(connection->basicEvent.fd);
		connection->basicEvent.fd = INVALID_SOCKET;
	}

	if( connection->closeCB ) {
		connection->closeCB(connection);
	}
}

static inline void le_serverOver(le_TcpServer* server) {
	LE_DECREASE_EVENTS(server->loop);
	server->masks &= ~LE_LISTENING;

	if( server->closeCB ) {
		server->closeCB(server);
	}
}

#define le_forceCloseConnection(connection)   \
	if( !(connection->masks & LE_CLOSING) ) { \
		le_connectionClose(connection);       \
	}

static inline void le_initWriteReq(le_TcpConnection* connection, le_WriteReq* req, le_Buffer bufs[], int bufCount, le_writeCB cb) {
	assert(cb);

	req->writeCB = cb;
	req->bufCount = bufCount;
	req->bufIndex = 0;
	req->totalSize = 0;
	req->connection = connection;
}

static inline void le_initBasicEvent(le_TcpBasicEvent* basicEvent,le_processCB processCB) {
	basicEvent->fd = INVALID_SOCKET;
	basicEvent->mask = 0;
	basicEvent->processCB = processCB;
}

static inline void le_serverProcessCB(le_TcpBasicEvent* event, unsigned events);
void le_tcpServerInit(le_EventLoop* loop, le_TcpServer* server, le_connectionCB connectionCB, le_serverCloseCB closeCB) {
	assert(connectionCB);
	assert(!loop->server);

	loop->server = server;
	server->loop = loop;
	server->masks = 0;
	server->pendingAcceptFd = INVALID_SOCKET;
	server->connectionCB = connectionCB;
	server->closeCB = closeCB;

	le_initBasicEvent(&server->basicEvent, le_serverProcessCB);
}

static inline void le_connectionProcessCB(le_TcpBasicEvent* event, unsigned events);
void le_tcpConnectionInit(le_EventLoop* loop, le_TcpConnection* connection, le_connectionCloseCB closeCB) {
	connection->loop = loop;
	connection->masks = 0;
	connection->pendingWriteReqs = 0;
	connection->writeTotalSize = 0;
	connection->closeCB = closeCB;

	le_initBasicEvent(&connection->basicEvent, le_connectionProcessCB);

	le_queueInit(&connection->writeReqHead);
}

static inline void le_channelProcessCB(le_TcpBasicEvent* basicEvent, unsigned events);
static inline void le_eventLoopInit(le_EventLoop* loop) {
	loop->epollFd = epoll_create1(EPOLL_CLOEXEC);
	if( loop->epollFd == - 1 ) {
		le_abort("create epoll fail!(error no: %d)", errno);
	}

	loop->server = NULL;
	loop->eventsCount = 0;
	loop->errorCode = 0;
	loop->posting = 0;
	loop->maxEventCount = LE_DEFAULT_EVENTS;
	loop->events = (struct epoll_event*)le_malloc(sizeof(struct epoll_event) * loop->maxEventCount);

	le_initBasicEvent(&loop->channelEvent, le_channelProcessCB);

	le_updateNowTime(loop);
	loop->timerHeap = le_timerHeapNew(32);

	le_queueInit(&loop->channelHead);
	le_queueInit(&loop->connectionsHead);
	le_safeQueueInit(&loop->pendingChannels);
}

static inline int le_addEvent(le_EventLoop* loop, le_TcpBasicEvent* event, unsigned events) {
	struct epoll_event ee;
	unsigned mask = event->mask | events;

	if( mask != event->mask ) {
		int op = event->mask != 0? EPOLL_CTL_MOD: EPOLL_CTL_ADD;

		event->mask = mask;
		ee.events = mask;
		ee.data.u64 = 0;
		ee.data.ptr = event;

		epoll_ctl(loop->epollFd, op, event->fd, &ee);
	}

	return 0;
}

static inline int le_delEvent(le_EventLoop* loop,  le_TcpBasicEvent* event, unsigned events) {
	struct epoll_event ee;
	unsigned mask = event->mask & (~events);

	if( mask != event->mask ) {
		int op = mask != 0? EPOLL_CTL_MOD: EPOLL_CTL_DEL;

		event->mask = mask;
		ee.events = mask;
		ee.data.u64 = 0;
		ee.data.ptr = event;

		epoll_ctl(loop->epollFd, op, event->fd, &ee);
	}

	return 0;
}

static inline void le_addConnection(le_EventLoop* loop, le_TcpConnection* connection) {
	connection->masks |= LE_CONNECTION;
	le_queueAdd(&loop->connectionsHead, &connection->connectionNode);
	LE_INCREASE_EVENTS(loop);
}

static inline void le_initWriteReqBufs(le_WriteReq* req, const le_Buffer bufs[], unsigned bufCount) {
	unsigned i;
	size_t size = 0;
	le_Buffer* reqBufs = req->bufs;

	for(i = 0; i < bufCount; ++i) {
		size += bufs[i].len;
		reqBufs[i].len = bufs[i].len;
		reqBufs[i].base = bufs[i].base;
	}

	req->totalSize = size;
}

static inline int le_checkSocketError(int fd) {
	int error;
	socklen_t errorLen = sizeof(error);

    if( getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &errorLen) == -1 ) {
        return errno;
    }

	return error;
}

static inline void le_connectorProcessCB(le_TcpBasicEvent* basicEvent, unsigned events) {
	int error;
    le_TcpConnection* connection = (le_TcpConnection*)basicEvent;

	LE_DECREASE_EVENTS(connection->loop);
	le_delEvent(connection->loop, basicEvent, EPOLLOUT);

	error = le_checkSocketError(basicEvent->fd);
  	if( error == 0 ) {
		basicEvent->processCB = le_connectionProcessCB;
		le_addConnection(connection->loop, connection);
		connection->connectCB(connection, LE_OK);
	} else {
		if( error == EINPROGRESS ) {
			return;
		}

		errno = error;
		le_setErrorCode(connection->loop, errno);
		connection->connectCB(connection, LE_ERROR);
		le_forceCloseConnection(connection);
	}
}

int le_connect(le_TcpConnection* connection, const char* ip, int port, le_connectCB cb) {
	int result;
	struct sockaddr_in addr;
	le_TcpBasicEvent* basicEvent;

	assert(cb);

	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(ip);

	basicEvent = &connection->basicEvent;
	if( basicEvent->fd != INVALID_SOCKET ) {
		le_setErrorCode(connection->loop, EINVAL);
		return LE_ERROR;
	}

	basicEvent->fd = socket(AF_INET, SOCK_STREAM, 0);
	if( basicEvent->fd == INVALID_SOCKET ) {
		le_setErrorCode(connection->loop, errno);
		return LE_ERROR;
	}

	if( le_setNonBlocking(connection->loop, basicEvent->fd) == LE_ERROR ) {
		le_setErrorCode(connection->loop, errno);
		return LE_ERROR;
	}

	while( 1 ) {
		result = connect(basicEvent->fd, (struct sockaddr *)&addr, sizeof(addr));
		if( result == -1 ) {
			if( errno == EINTR ) {
				continue;
			} else if( errno != EINPROGRESS ) {
				le_setErrorCode(connection->loop, errno);
				return LE_ERROR;
			}
		}
		break;
	}

	basicEvent->processCB = le_connectorProcessCB;
	connection->connectCB = cb;
	le_addEvent(connection->loop, basicEvent, EPOLLOUT);
	LE_INCREASE_EVENTS(connection->loop);

	return LE_OK;
}

static inline void le_deleteWriteReqs(le_TcpConnection* connection, int status) {
	le_Queue* node;
	le_Queue* head;
	le_WriteReq* req;

	head = &connection->writeReqHead;

	while( !le_queueEmpty(head) ) {
		node = le_queueNext(head);
		req = LE_CONTAINING_RECORD(node, le_WriteReq, writeReqNode);
		le_queueRemove(node);

		if( req->bufs != req->bufsml ) {
			free(req->bufs);
		}
		req->bufs = NULL;

		req->writeCB(req, status);
	}

	connection->writeTotalSize = 0;
}

static inline void le_writeComplete(le_WriteReq* req, int status) {
    le_TcpConnection* connection = req->connection;

    le_queueRemove(&req->writeReqNode);

    if( req->bufs != req->bufsml ) {
        free(req->bufs);
    }
    req->bufs = NULL;

    if( status == 0 ) {
        req->writeCB(req, req->totalSize);
		if( le_queueEmpty(&connection->writeReqHead) ) {
			assert(connection->writeTotalSize == 0);
			le_delEvent(connection->loop, &connection->basicEvent, EPOLLOUT);

			if( connection->masks & LE_CLOSING ) {
				le_connectionOver(connection);
			} else if( connection->masks & LE_SHUTDOWN_WRITE ) {
				shutdown(connection->basicEvent.fd, SHUT_WR);
			}
		}
    } else {
		le_delEvent(connection->loop, &connection->basicEvent, EPOLLOUT);
		le_setErrorCode(connection->loop, errno);
		connection->masks |= LE_SHUTDOWN_WRITE;
        req->writeCB(req, status);

		le_deleteWriteReqs(connection, status);
		le_forceCloseConnection(connection);
    }
}

static void le_processWriting(le_TcpConnection* connection) {
	ssize_t n;
	le_Queue* node;
	le_WriteReq* req;
	unsigned bufCount;

	if( le_queueEmpty(&connection->writeReqHead) ) {
		return;
	}

	node = le_queueNext(&connection->writeReqHead);
	req = LE_CONTAINING_RECORD(node, le_WriteReq, writeReqNode);

	assert(req->bufIndex < req->bufCount);

	bufCount = req->bufCount - req->bufIndex;

	if( bufCount == 1 ) {
		n = write(connection->basicEvent.fd, req->bufs[0].base, req->bufs[0].len );
	} else {
		n = writev(connection->basicEvent.fd, (struct iovec*)req->bufs, bufCount);
	}

	if( n >= 0 ) {
		size_t len;
		le_Buffer* buf;

		do {
			buf = &(req->bufs[req->bufIndex]);
			connection->writeTotalSize -= n;

			if( (size_t)n < buf->len ) {
				buf->base += n;
				buf->len -= n;
				break;
			} else {
				n -= buf->len;
				req->bufIndex++;

				if( req->bufIndex == req->bufCount ) {
					// write over!!!!
					assert(n == 0);
					le_writeComplete(req, LE_OK);
					break;
				}
			}
		} while( n >= 0 );
	} else {
		if( errno != EAGAIN && errno != EWOULDBLOCK ) {
			le_setErrorCode(connection->loop, errno);
			le_writeComplete(req, LE_ERROR);
		}
	}
}

int le_write(le_TcpConnection* connection, le_WriteReq* req, le_Buffer bufs[], int bufCount, le_writeCB cb) {
	int bufRemainSize;

	if( !(connection->masks & LE_CONNECTION) ) {
		le_setErrorCode(connection->loop, EINVAL);
		return LE_ERROR;
	}

	if( connection->masks & LE_SHUTDOWN_WRITE ) {
		le_setErrorCode(connection->loop, ESHUTDOWN);
		return LE_ERROR;
	}

	if( bufCount > MAX_BUFS_COUNT || bufCount < 1 ) {
		le_setErrorCode(connection->loop, EINVAL);
		return LE_ERROR;
	}

	le_initWriteReq(connection, req, bufs, bufCount, cb);

	if( bufCount <= LE_ARRAY_SIZE(req->bufsml) ) {
		req->bufs = req->bufsml;
	} else {
		req->bufs = malloc(bufCount * sizeof(le_Buffer));
	}

	le_initWriteReqBufs(req, bufs, bufCount);
	bufRemainSize = connection->writeTotalSize;
	le_queueAdd(&connection->writeReqHead, &req->writeReqNode);
	connection->writeTotalSize += req->totalSize;

	if( bufRemainSize == 0 ) {
		le_processWriting(connection);
		if( connection->writeTotalSize > 0 ) {
			le_addEvent(connection->loop, &connection->basicEvent, EPOLLOUT);
		}
	}

	return LE_OK;
}

int le_startRead(le_TcpConnection* connection, le_readCB readCB, le_allocCB allocCB) {
	assert(readCB);
	assert(allocCB);

	if( !(connection->masks & LE_CONNECTION) ) {
		le_setErrorCode(connection->loop, EINVAL);
		return LE_ERROR;
	}

	if( connection->masks & LE_READING ) {
		le_setErrorCode(connection->loop, EALREADY);
		return LE_ERROR;
	}

	connection->readCB = readCB;
	connection->allocCB = allocCB;
	connection->masks |= LE_READING;

	le_addEvent(connection->loop, &connection->basicEvent, EPOLLIN);

	return LE_OK;
}

int le_stopRead(le_TcpConnection* connection) {
	if( !(connection->masks & LE_CONNECTION) ) {
		le_setErrorCode(connection->loop, EINVAL);
		return LE_ERROR;
	}

	if( !(connection->masks & LE_READING) ) {
		le_setErrorCode(connection->loop, EALREADY);
		return LE_ERROR;
	}

	connection->masks &= ~LE_READING;
	le_delEvent(connection->loop, &connection->basicEvent, EPOLLIN);

	return LE_OK;
}

static void le_processReading(le_TcpConnection* connection) {
	ssize_t bytes;
	le_Buffer buf;

	assert(connection->masks & LE_READING);
	connection->masks |= LE_QUEUE_READ;

	connection->allocCB(connection, &buf);

	bytes = read(connection->basicEvent.fd, buf.base, buf.len);

	if( bytes > 0 ) {
		connection->readCB(connection, bytes, buf.base);
	} else if( bytes == 0 ) {
		// socket closed
		connection->masks &= ~LE_READING;
		le_setErrorCode(connection->loop, LE_OK);
		connection->readCB(connection, LE_ERROR, buf.base);
		le_forceCloseConnection(connection);
	} else {
		// error
		connection->masks &= ~LE_READING;
		le_setErrorCode(connection->loop, errno);
		connection->readCB(connection, LE_ERROR, buf.base);
		le_forceCloseConnection(connection);
	}

	connection->masks &= ~LE_QUEUE_READ;

	if( (connection->masks & LE_CLOSING) && (le_queueEmpty(&connection->writeReqHead)) ) {
		le_connectionOver(connection);
	}
}

int le_accept(le_TcpServer* server, le_TcpConnection* client) {
	if( server->pendingAcceptFd == INVALID_SOCKET ) {
		le_setErrorCode(server->loop, EINVAL);
		return LE_ERROR;
	}

	le_addConnection(server->loop, client);
	client->basicEvent.fd = server->pendingAcceptFd;
	server->pendingAcceptFd = INVALID_SOCKET;

	return LE_OK;
}

static void le_closeAllConnections(le_TcpServer* server) {
	le_Queue* node;
	le_Queue* head;
	le_TcpConnection* connection;

	head = &server->loop->connectionsHead;

	while (!le_queueEmpty(head)) {
		node = le_queueNext(head);
		connection = LE_CONTAINING_RECORD(node, le_TcpConnection, connectionNode);

		le_forceCloseConnection(connection);
	}
}

int le_serverClose(le_TcpServer* server) {
	if( !(server->masks & LE_LISTENING) ) {
		le_setErrorCode(server->loop, EINVAL);
		return LE_ERROR;
	}

	if( server->basicEvent.fd == INVALID_SOCKET ) {
		le_setErrorCode(server->loop, ENOTSOCK);
		return LE_ERROR;
	}

    if( server->pendingAcceptFd != INVALID_SOCKET ) {
		close(server->pendingAcceptFd);
		server->pendingAcceptFd = INVALID_SOCKET;
	}

	server->masks &= ~LE_LISTENING;
	close(server->basicEvent.fd);
	server->basicEvent.fd = INVALID_SOCKET;

	le_closeAllConnections(server);
	le_serverOver(server);

	return LE_OK;
}

int le_connectionShutdown(le_TcpConnection* connection) {
	if( !(connection->masks & LE_CONNECTION) ) {
		le_setErrorCode(connection->loop, EINVAL);
		return LE_ERROR;
	}

	if( connection->masks & LE_SHUTDOWN_WRITE ) {
		le_setErrorCode(connection->loop, ESHUTDOWN);
		return LE_ERROR;
	}

	connection->masks |= LE_SHUTDOWN_WRITE;
	if( le_queueEmpty(&connection->writeReqHead) ) {
        shutdown(connection->basicEvent.fd, SHUT_WR);
	}

	return LE_OK;
}

int le_connectionClose(le_TcpConnection* connection) {
	if( connection->masks & LE_CLOSING ) {
		le_setErrorCode(connection->loop, EINVAL);
		return LE_ERROR;
	}

	if( connection->basicEvent.fd == INVALID_SOCKET ) {
		le_setErrorCode(connection->loop, ENOTSOCK);
		return LE_ERROR;
	}

	connection->masks |= LE_CLOSING;
	connection->masks |= LE_SHUTDOWN_WRITE;

	if( !(connection->masks & LE_QUEUE_READ) && le_queueEmpty(&connection->writeReqHead) ) {
        le_connectionOver(connection);
	} else {
		close(connection->basicEvent.fd);
		connection->basicEvent.fd == INVALID_SOCKET;
	}
	//} else if( connection->masks & LE_READING ) {
	//	le_stopRead(connection);
	//}

	return LE_OK;
}

static inline void le_connectionProcessCB(le_TcpBasicEvent* basicEvent, unsigned events) {
	le_TcpConnection* connection = (le_TcpConnection*)basicEvent;

	if( events & EPOLLIN ) {
		le_processReading(connection);
	}

	if( events & EPOLLOUT ) {
		le_processWriting(connection);
	}
}

static inline void le_serverProcessCB(le_TcpBasicEvent* basicEvent, unsigned events) {
	int afd;
	struct sockaddr_in addr;
	socklen_t len = sizeof(struct sockaddr_in);
	le_TcpServer* server = (le_TcpServer*)basicEvent;

	while( 1 ) {
        afd = accept(basicEvent->fd, (struct sockaddr*)&addr, &len);
        if( afd == INVALID_SOCKET ) {
			if( errno == EINTR ) {
                continue;
			} else {
				le_setErrorCode(server->loop, errno);
				server->connectionCB(server, LE_ERROR);
                return;
            }
        }
        break;
    }

    if( le_setNonBlocking(server->loop, afd) == LE_ERROR ) {
		le_setErrorCode(server->loop, errno);
		server->connectionCB(server, LE_ERROR);
		return;
	}

	server->pendingAcceptFd	= afd;
	server->connectionCB(server, LE_OK);

	if( server->pendingAcceptFd != INVALID_SOCKET ) {
		le_delEvent(server->loop, basicEvent, EPOLLIN);
	}
}

static inline void le_addChannel(le_EventLoop* loop, le_Channel* channel) {
	channel->masks |= LE_CHANNEL_WORKING;
	le_queueAdd(&loop->channelHead, &channel->channelNode);
	LE_INCREASE_EVENTS(loop);
}

static inline void le_channelOver(le_Channel* channel) {
	le_queueRemove(&channel->channelNode);
	LE_DECREASE_EVENTS(channel->loop);

	channel->masks &= ~LE_CHANNEL_WORKING;

	if( channel->closeCB ) {
		channel->closeCB(channel);
	}
}

static inline void le_channelProcessCB(le_TcpBasicEvent* basicEvent, unsigned events) {
	ssize_t bytes;
	uint64_t value = 0;
	le_EventLoop* loop;
	le_Queue* channelNode;
	le_Queue activityChannels;
	le_Channel* activityChannel;

	bytes = read(basicEvent->fd, &value, sizeof(value));

	if( bytes != sizeof(value) ) {
		le_abort("le_channelProcessCB fail!(error no: %u, %s)\n", errno, le_strerror(errno));
	}

	loop = LE_CONTAINING_RECORD(basicEvent, le_EventLoop, channelEvent);
	loop->posting = 0;

	le_safeQueueSwap(&loop->pendingChannels, &activityChannels);

	channelNode = le_queueNext(&activityChannels);
	while( channelNode != &activityChannels ) {
		activityChannel = LE_CONTAINING_RECORD(channelNode, le_Channel, pendingNode);
		channelNode = le_queueNext(channelNode); //avoid channelNode invalid when channel closed

		activityChannel->pending = 0;

		if( activityChannel->masks & LE_CLOSING ) {
			le_channelOver(activityChannel);
		} else {
			activityChannel->channelCB(activityChannel, LE_OK); // may be channel have closed
		}
	}
}

static inline int le_channelEventInit(le_EventLoop* loop) {
	int eventFd;
	le_TcpBasicEvent* channelEvent = &loop->channelEvent;

	if( channelEvent->fd != INVALID_SOCKET ) {
		return LE_OK;
	}

	// int eventfd(unsigned int initval, int flags);
	// eventfd better than pipe;
	// creates an "eventfd object" that can be used as an event
    // wait/notify mechanism by user-space applications, and by the kernel
    // to notify user-space applications of events.  The object contains an
    // unsigned 64-bit integer (uint64_t) counter that is maintained by the
    // kernel.  This counter is initialized with the value specified in the
    // argument initval.
	eventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if( eventFd == INVALID_SOCKET ) {
		return LE_ERROR;
	}

	channelEvent->fd = eventFd;

	le_addEvent(loop, channelEvent, EPOLLIN);

	return LE_OK;
}

int le_channelInit(le_EventLoop* loop, le_Channel* channel, le_channelCB channelCB, le_channelCloseCB closeCB) {
	assert(channelCB);

	if( le_channelEventInit(loop) == LE_ERROR ) {
		le_setErrorCode(loop, errno);
		return LE_ERROR;
	}

	channel->loop = loop;
	channel->masks = 0;
	channel->pending = 0;
	channel->channelCB = channelCB;
	channel->closeCB = closeCB;
	le_queueInit(&channel->channelNode);
	le_queueInit(&channel->pendingNode);

	le_addChannel(loop, channel);

	return LE_OK;
}

int le_channelPost(le_Channel* channel) {
	if( channel->masks & LE_CLOSING ) {
		return LE_ERROR;
	}

	if( LE_ACCESS_ONCE(long, channel->pending) == 1 ) { // cheap check and stop compiler from optimizing
		return LE_OK;
	}

	if( __sync_val_compare_and_swap(&channel->pending, 0, 1) == 0 ) {
		int bytes;
		le_EventLoop* loop = channel->loop;

		le_safeQueueAdd(&loop->pendingChannels, &channel->pendingNode);

		if( LE_ACCESS_ONCE(long, loop->posting) == 1 ) { // cheap check and stop compiler from optimizing
			return LE_OK;
		}

		if( __sync_val_compare_and_swap(&loop->posting, 0, 1) == 1 ) {
			return LE_OK;
		}

		bytes = write(loop->channelEvent.fd, (void*)&le_channelBytes, sizeof(le_channelBytes)); // wake up loop

		if( bytes == sizeof(le_channelBytes) ) {
			return LE_OK;
		} else if( bytes == -1 ) {
			if( errno == EAGAIN || errno == EWOULDBLOCK ) {
				return LE_OK;
			}
		}

		le_abort("le_channelPost fail!(error no: %u, %s)\n", errno, le_strerror(errno));
	}

	return LE_OK;
}

void le_channelClose(le_Channel* channel) {
	if( channel->masks & LE_CLOSING ) {
		return;
	}

	if( !(channel->masks & LE_CHANNEL_WORKING) ) {
		return;
	}

	channel->masks |= LE_CLOSING;

	if( !channel->pending ) {
		le_channelOver(channel);
	}
}

int le_bind(le_TcpServer* server, const char * ip, int port) {
	int on;
	le_TcpBasicEvent* basicEvent;
	struct sockaddr_in addr;

	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(ip);

	basicEvent = &server->basicEvent;
	if( basicEvent->fd != INVALID_SOCKET ) {
		le_setErrorCode(server->loop, EINVAL);
		return LE_ERROR;
	}

	basicEvent->fd = socket(AF_INET, SOCK_STREAM, 0);
	if( basicEvent->fd == INVALID_SOCKET ) {
		le_setErrorCode(server->loop, errno);
		return LE_ERROR;
	}

	if( le_setNonBlocking(server->loop, basicEvent->fd) == LE_ERROR ) {
		le_setErrorCode(server->loop, errno);
		return LE_ERROR;
	}

	on = 1;
	if( setsockopt(basicEvent->fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) ) {
		le_setErrorCode(server->loop, errno);
    	return LE_ERROR;
	}

	if( bind(basicEvent->fd, (struct sockaddr*)&addr, sizeof(struct sockaddr)) != 0 ) {
		le_setErrorCode(server->loop, errno);
		return LE_ERROR;
	}

	return LE_OK;
}

int le_listen(le_TcpServer* server, int backlog) {
	le_TcpBasicEvent* basicEvent = &server->basicEvent;

	if( basicEvent->fd == INVALID_SOCKET ) {
		le_setErrorCode(server->loop, EINVAL);
		return LE_ERROR;
	}

	if( listen(basicEvent->fd, backlog) != 0 ) {
		le_setErrorCode(server->loop, errno);
		return LE_ERROR;
	}

	server->masks |= LE_LISTENING;
	LE_INCREASE_EVENTS(server->loop);
	le_addEvent(server->loop, basicEvent, EPOLLIN);

	return LE_OK;
}

static inline void le_poll(le_EventLoop* loop, le_time_t timeout) {
	int i, n;
	le_TcpBasicEvent* basicEvent;
	struct epoll_event* events = loop->events;

	n = epoll_wait(loop->epollFd, events, loop->maxEventCount, timeout);

    if( n == -1 ) {
		if( errno == EINTR ) {
			return;
		}

		le_abort("epoll wait error!(error no:%d)", errno);
    }

    for(i = 0; i < n; ++i) {
        basicEvent = (le_TcpBasicEvent*)events[i].data.ptr;
        basicEvent->processCB(basicEvent, events[i].events);
    }

    if( (n == loop->maxEventCount) && (loop->maxEventCount < LE_EVENTS_LIMIT) ) {
        loop->maxEventCount <<= 1;
        loop->events = (struct epoll_event*)le_realloc(loop->events, sizeof(struct epoll_event) * loop->maxEventCount);
    }
}

extern void le_processTimers(le_EventLoop* loop);
static inline void le_processEvents(le_EventLoop* loop) {
	le_time_t timeout;
	le_Timer* timer;

	while( loop->eventsCount ) {
		le_updateNowTime(loop);
		le_processTimers(loop);

		timer = le_timerHeapMin(loop->timerHeap);
		if( timer ) {
			timeout = timer->timeout - loop->time;
			if( timeout < 0 ) {
				timeout = 0;
			}
		} else {
			if( loop->eventsCount == 0 ) {
				break;
			}
			timeout = -1;
		}

		le_poll(loop, timeout);
	}
}

void le_run(le_EventLoop* loop) {
	le_processEvents(loop);
}

le_EventLoop* le_eventLoopCreate() {
	le_EventLoop* loop = (le_EventLoop*)le_malloc(sizeof(le_EventLoop));
	if( !loop ) {
		le_abort("create loop fail! out of memory!");
	}

	le_eventLoopInit(loop);

	return loop;
}

void le_eventLoopDelete(le_EventLoop* loop) {
	le_timerHeapFree(loop->timerHeap);
	le_safeQueueDestroy(&loop->pendingChannels);
	if( loop->channelEvent.fd != INVALID_SOCKET ) {
		close(loop->channelEvent.fd);
	}
	close(loop->epollFd);
	le_free(loop->events);
	le_free(loop);
}

#undef IOV_MAX
#undef INVALID_SOCKET
#undef le_forceCloseConnection
