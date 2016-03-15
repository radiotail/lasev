//////////////////////////////////////////////////////////////////////////////////////
// Mail: radiotail86@gmail.com
// About the details of license, please read LICENSE
//////////////////////////////////////////////////////////////////////////////////////

#include "lasev.h"
#include "le_internal.h"
#include "le_timerHeap.h"

static le_Buffer le_sharedZeroBuf = {0, ""};
static volatile ULONG le_initFlag = 0;

#define LE_SUCCEEDED_WITH_IOCP(result)	 \
	((result) || (GetLastError() == ERROR_IO_PENDING))

#define LE_GET_WRITE_BYTES(req)	req->overlapped.InternalHigh

typedef BOOL (WINAPI *sGetQueuedCompletionStatusEx)
             (HANDLE CompletionPort,
              LPOVERLAPPED_ENTRY lpCompletionPortEntries,
              ULONG ulCount,
              PULONG ulNumEntriesRemoved,
              DWORD dwMilliseconds,
              BOOL fAlertable);
sGetQueuedCompletionStatusEx pGetQueuedCompletionStatusEx;

static char le_strerrorBuf[128];
const char* le_strerror(int err) {
	DWORD size = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM |
							   FORMAT_MESSAGE_IGNORE_INSERTS,
							   NULL,
							   err,
							   0,
							   le_strerrorBuf,
							   128,
							   NULL);

	if( size == 0 ) {
		return (const char*)strerror(err);
	} else if( size > 2 && le_strerrorBuf[size - 2] == '\r' ) {
		le_strerrorBuf[size - 2] = '\0'; // remove \r
	}

	return (const char*)le_strerrorBuf;
}

#define LE_NT_SUCCESS(req) (req)->overlapped.Internal == 0
#define LE_NT_ERROR(req) (req)->overlapped.Internal > 0

// template function for req
#define le_setErrorCodeByNtStatus(loop, req) \
	do{					\
		DWORD bytes;	\
		GetOverlappedResult(NULL, &req->overlapped, &bytes, FALSE); \
		loop->errorCode = GetLastError();	\
	} while(0)

int le_getErrorCode(le_EventLoop* loop) {
	return loop->errorCode;
}

static inline void le_updateNowTime(le_EventLoop* loop) {
	loop->time = GetTickCount();
}

static inline void le_initAcceptReq(le_TcpServer* server, le_AcceptReq* req) {
	req->type = LE_ACCEPT;
	req->socket = INVALID_SOCKET;
}

static inline void le_initReadReq(le_TcpConnection* connection, le_ReadReq* req) {
	req->type = LE_READ;
	req->connection = connection;
}

static inline void le_initWriteReq(le_TcpConnection* connection, le_WriteReq* req, le_writeCB cb) {
	assert(cb);
	req->type = LE_WRITE;
	req->writeCB = cb;
	req->connection = connection;
}

static inline void le_initConnectReq(le_TcpConnection* connection, le_ConnectReq* req) {
	req->type = LE_CONNECT;
	req->connection = connection;
}

static inline void le_addConnection(le_EventLoop* loop, le_TcpConnection* connection) {
	connection->masks |= LE_CONNECTION;
	le_queueAdd(&loop->connectionsHead, &connection->connectionNode);
	LE_INCREASE_EVENTS(loop);
}

static inline void le_connectionOver(le_TcpConnection* connection) {
	if( connection->masks & LE_CONNECTION ) {
		connection->masks &= ~LE_CONNECTION;
		le_queueRemove(&connection->connectionNode);
		LE_DECREASE_EVENTS(connection->loop);
	}

	if( connection->socket != INVALID_SOCKET ) {
		closesocket(connection->socket);
		connection->socket = INVALID_SOCKET;
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

int le__setTcpNoDelay(le_EventLoop* loop, int socket, int enable) {
	if( socket == INVALID_SOCKET ) {
		le_setErrorCode(loop, WSAEINVAL);
		return LE_ERROR;
	}

	if( setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&enable, sizeof(enable)) == SOCKET_ERROR ) {
		le_setErrorCode(loop, WSAGetLastError());
		return LE_ERROR;
	}

	return LE_OK;
}

int le__setTcpKeepAlive(le_EventLoop* loop, int socket, int enable) {
	if( socket == INVALID_SOCKET ) {
		le_setErrorCode(loop, WSAEINVAL);
		return LE_ERROR;
	}

	if( setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, (const char*)&enable, sizeof(enable) ) == SOCKET_ERROR ) {
		le_setErrorCode(loop, WSAGetLastError());
		return LE_ERROR;
	}

	return LE_OK;
}

int le__setTcpSendBuffer(le_EventLoop* loop, int socket, int size) {
	if( socket == INVALID_SOCKET ) {
		le_setErrorCode(loop, WSAEINVAL);
		return LE_ERROR;
	}

	if( setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (const char*)&size, sizeof(size) ) == SOCKET_ERROR ) {
		le_setErrorCode(loop, WSAGetLastError());
		return LE_ERROR;
	}

	return LE_OK;
}

static inline void le_eventLoopInit(le_EventLoop* loop) {
	loop->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
	if( !loop->iocp ) {
		le_abort("create iocp fail!(error no: %d)", GetLastError());
	}

	loop->server = NULL;
	loop->eventsCount = 0;
	loop->errorCode = 0;
	loop->posting = 0;
	loop->acceptex = NULL;
	loop->connectex = NULL;
	loop->channelReq.type = LE_POST;
	loop->maxOverlappeds = LE_OVERLAPEDS_COUNT;
	loop->overlappeds = (LPOVERLAPPED_ENTRY)le_malloc((pGetQueuedCompletionStatusEx? loop->maxOverlappeds: 1) * sizeof(OVERLAPPED_ENTRY));

	le_updateNowTime(loop);
	loop->timerHeap = le_timerHeapNew(32);

	le_queueInit(&loop->channelHead);
	le_queueInit(&loop->connectionsHead);
	le_safeQueueInit(&loop->pendingChannels);
}

void le_tcpServerInit(le_EventLoop* loop, le_TcpServer* server, le_connectionCB connectionCB, le_serverCloseCB closeCB) {
	assert(connectionCB);
	assert(!loop->server);

	loop->server = server;
	server->loop = loop;
	server->masks = 0;
	server->socket = INVALID_SOCKET;
	server->pendingAcceptReq = NULL;
	server->pendingAcceptReqs = 0;
	server->connectionCB = connectionCB;
	server->closeCB = closeCB;
}

void le_tcpConnectionInit(le_EventLoop* loop, le_TcpConnection* connection, le_connectionCloseCB closeCB) {
	connection->loop = loop;
	connection->masks = 0;
	connection->socket = INVALID_SOCKET;
	connection->pendingWriteReqs = 0;
	connection->closeCB = closeCB;
}

static inline void* le_getWinsockFuncEx(SOCKET socket, GUID guid) {
  	DWORD bytes;
	void* pfunc = NULL;

  	WSAIoctl(socket,
             SIO_GET_EXTENSION_FUNCTION_POINTER,
             &guid,
             sizeof(guid),
             &pfunc,
             sizeof(pfunc),
             &bytes,
             NULL,
             NULL);

    return pfunc;
}

static inline int le_associateWithIocp(le_EventLoop* loop, SOCKET socket) {
	DWORD yes = 1;
	assert(socket != INVALID_SOCKET);

 	if( ioctlsocket(socket, FIONBIO, &yes) == SOCKET_ERROR ) {
 		le_setErrorCode(loop, WSAGetLastError());
 		return LE_ERROR;
 	}

	if( !SetHandleInformation((HANDLE)socket, HANDLE_FLAG_INHERIT, 0) ) {
		le_setErrorCode(loop, WSAGetLastError());
		return LE_ERROR;
	}

	if( CreateIoCompletionPort((HANDLE)socket,
							   loop->iocp,
							   (ULONG_PTR)socket,
							   0) == NULL ) {
		le_setErrorCode(loop, WSAGetLastError());
		return LE_ERROR;
	}

	return LE_OK;
}

static inline int le_bindForConnecting(le_EventLoop* loop, le_TcpConnection* connection) {
	struct sockaddr_in bindAddr;
	if( le_associateWithIocp(loop, connection->socket) == LE_ERROR ) {
		return LE_ERROR;
	}

	memset(&bindAddr, 0, sizeof(struct sockaddr_in));
	bindAddr.sin_family = AF_INET;
	bindAddr.sin_port = 0;
	bindAddr.sin_addr.s_addr = INADDR_ANY;

	if( bind(connection->socket, (struct sockaddr*)&bindAddr, sizeof(struct sockaddr)) == SOCKET_ERROR ) {
		le_setErrorCode(loop, WSAGetLastError());
		closesocket(connection->socket);
		return LE_ERROR;
	}

	return LE_OK;
}

int le_connect(le_TcpConnection* connection, const char* ip, int port, le_connectCB cb) {
	int result;
	le_ConnectReq* req;
	struct sockaddr_in serverAddr;
	le_EventLoop* loop = connection->loop;

	assert(cb);

	if( connection->masks & (LE_CONNECTING | LE_CONNECTION) ) {
		le_setErrorCode(loop, WSAEINVAL);
		return LE_ERROR;
	}

	connection->socket = socket(AF_INET, SOCK_STREAM, 0);
	if( connection->socket == INVALID_SOCKET ) {
		le_setErrorCode(loop, WSAGetLastError());
		return LE_ERROR;
	}

	if( le_bindForConnecting(loop, connection) == LE_ERROR ) {
		return LE_ERROR;
	}

	if( loop->connectex == NULL ) {
		const GUID wsaidConnectex = WSAID_CONNECTEX;
		loop->connectex = (LPFN_CONNECTEX)le_getWinsockFuncEx(connection->socket, wsaidConnectex);
		if( loop->connectex == NULL ) {
			le_setErrorCode(loop, WSAGetLastError());
			return LE_ERROR;
		}
	}

	connection->connectCB = cb;
	req = &connection->connectReq;
	le_initConnectReq(connection, req);
	memset(&req->overlapped, 0, sizeof(req->overlapped));

	memset(&serverAddr, 0, sizeof(struct sockaddr_in));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	serverAddr.sin_addr.s_addr = inet_addr(ip);

	result = loop->connectex(connection->socket,
							 (struct sockaddr*)&serverAddr,
							 sizeof(struct sockaddr),
							 NULL,
							 0,
							 NULL,
							 &req->overlapped);

	if( LE_SUCCEEDED_WITH_IOCP(result) ) {
		connection->masks |= LE_CONNECTING;
		LE_INCREASE_EVENTS(loop);
	} else {
		le_setErrorCode(loop, WSAGetLastError());
		return LE_ERROR;
	}

	return LE_OK;
}

static inline void le_processConnectReq(le_TcpConnection* connection, le_ConnectReq* req) {
	connection->masks &= ~LE_CONNECTING;
	LE_DECREASE_EVENTS(connection->loop);

	if( LE_NT_SUCCESS(req) ) {
		if( setsockopt(connection->socket,
					   SOL_SOCKET,
					   SO_UPDATE_CONNECT_CONTEXT,
					   NULL,
					   0) == ERROR_SUCCESS ) {
			le_addConnection(connection->loop, connection);
			connection->connectCB(connection, LE_OK);
		} else {
			le_setErrorCode(connection->loop, WSAGetLastError());
			connection->connectCB(connection, LE_ERROR);
			le_forceCloseConnection(connection);
		}
	} else {
		le_setErrorCodeByNtStatus(connection->loop, req);
		connection->connectCB(connection, LE_ERROR);
		le_forceCloseConnection(connection);
	}
}

static inline int le_queueAccept(le_TcpServer* server, le_AcceptReq* req) {
	int result;
	DWORD bytes;
	SOCKET acceptSocket;
	le_EventLoop* loop = server->loop;

	acceptSocket = socket(AF_INET, SOCK_STREAM, 0);
	if( acceptSocket == INVALID_SOCKET ) {
		le_setErrorCode(loop, WSAGetLastError());
		server->connectionCB(server, LE_ERROR);
		return LE_ERROR;
	}

	memset(&req->overlapped, 0, sizeof(req->overlapped));

	result = loop->acceptex(server->socket,
						 	acceptSocket,
						 	(void*)req->buffer,
						 	0,
						 	sizeof(struct sockaddr_storage),
						 	sizeof(struct sockaddr_storage),
							&bytes,
							&req->overlapped);

	if( LE_SUCCEEDED_WITH_IOCP(result) ) {
		req->socket = acceptSocket;
		server->pendingAcceptReqs++;
	} else {
		le_setErrorCode(loop, WSAGetLastError());
		closesocket(acceptSocket);
		server->connectionCB(server, LE_ERROR);
		return LE_ERROR;
	}

	return LE_OK;
}

int le_accept(le_TcpServer* server, le_TcpConnection* connection) {
	le_AcceptReq* req = server->pendingAcceptReq;
	if( !req ) {
		le_setErrorCode(server->loop, WSAEINVAL);
		return LE_ERROR;
	}

	if( le_associateWithIocp(server->loop, req->socket) == LE_ERROR ) {
		closesocket(req->socket);
		req->socket = INVALID_SOCKET;
		return LE_ERROR;
	}

	le_addConnection(server->loop, connection);
	connection->socket = req->socket;
	req->socket = INVALID_SOCKET;

	return le_queueAccept(server, req);
}

static inline void le_processAcceptReq(le_TcpServer* server, le_AcceptReq* req) {
	if( LE_NT_SUCCESS(req) ) {
		if( setsockopt(req->socket,
					   SOL_SOCKET,
					   SO_UPDATE_ACCEPT_CONTEXT,
					   (const char*)&server->socket,
					   sizeof(server->socket)) == ERROR_SUCCESS ) {
			server->pendingAcceptReq = req;
			server->connectionCB(server, LE_OK);
			server->pendingAcceptReq = NULL;
		} else {
			le_setErrorCode(server->loop, WSAGetLastError());
			closesocket(req->socket);
			req->socket = INVALID_SOCKET;
			server->connectionCB(server, LE_ERROR);
			le_queueAccept(server, req);
		}
	} else {
		closesocket(req->socket);
		req->socket = INVALID_SOCKET;
		if( server->masks & LE_LISTENING ) {
			le_setErrorCodeByNtStatus(server->loop, req);
			server->connectionCB(server, LE_ERROR);
			le_queueAccept(server, req);
		}
	}

	if( (--server->pendingAcceptReqs == 0) && !(server->masks & LE_LISTENING) ) {
		le_serverOver(server);
	}
}

static inline int le_queueRead(le_TcpConnection* connection, le_ReadReq* req) {
	int result;
	DWORD flags = 0;

	memset(&req->overlapped, 0, sizeof(req->overlapped));

	result = WSARecv(connection->socket,
					 (WSABUF*)&le_sharedZeroBuf,
					 1,
					 NULL,
					 &flags,
					 &req->overlapped,
					 NULL);

	if( LE_SUCCEEDED_WITH_IOCP(result == ERROR_SUCCESS) ) {
		connection->masks |= LE_QUEUE_READ;
	} else {
		le_setErrorCode(connection->loop, WSAGetLastError());
		connection->readCB(connection, LE_ERROR, NULL);
		le_forceCloseConnection(connection);
		return LE_ERROR;
	}

	return LE_OK;
}

int le_startRead(le_TcpConnection* connection, le_readCB readCB, le_allocCB allocCB) {
	assert(readCB);
	assert(allocCB);

	if( !(connection->masks & LE_CONNECTION) ) {
		le_setErrorCode(connection->loop, WSAEINVAL);
		return LE_ERROR;
	}

	if( connection->masks & LE_READING ) {
		le_setErrorCode(connection->loop, WSAEALREADY);
		return LE_ERROR;
	}

	connection->readCB = readCB;
	connection->allocCB = allocCB;
	connection->masks |= LE_READING;

	le_initReadReq(connection, &connection->readReq);

	return le_queueRead(connection, &connection->readReq);
}

int le_stopRead(le_TcpConnection* connection) {
	if( !(connection->masks & LE_CONNECTION) ) {
		le_setErrorCode(connection->loop, WSAEINVAL);
		return LE_ERROR;
	}

	if( !(connection->masks & LE_READING) ) {
		le_setErrorCode(connection->loop, WSAEALREADY);
		return LE_ERROR;
	}

	connection->masks &= ~LE_READING;

	return LE_OK;
}

static inline void le_processReadReq(le_TcpConnection* connection, le_ReadReq* req) {
	le_Buffer buf;
	DWORD bytes, flags, err;

	if( LE_NT_SUCCESS(req) ) {
		do {
			flags = 0;
			connection->allocCB(connection, &buf);

			if( WSARecv(connection->socket,
						(WSABUF*)&buf,
						1,
						&bytes,
						&flags,
						NULL,
						NULL) == ERROR_SUCCESS ) {
				if( bytes > 0 ) {
					connection->readCB(connection, bytes, buf.base);
				} else {
					// socket closed
					connection->masks &= ~LE_READING;
					le_setErrorCode(connection->loop, LE_OK);
					connection->readCB(connection, LE_ERROR, buf.base);
					le_forceCloseConnection(connection);
					break;
				}
			} else {
				err = WSAGetLastError();
				if( err != WSAEWOULDBLOCK ) {
					connection->masks &= ~LE_READING;
					le_setErrorCode(connection->loop, err);
					connection->readCB(connection, LE_ERROR, buf.base);
					le_forceCloseConnection(connection);
				}
				break;
			}
		} while( bytes == buf.len ); // some data remains
	} else {
		le_setErrorCodeByNtStatus(connection->loop, req);
		connection->masks &= ~LE_READING;
		connection->readCB(connection, LE_ERROR, NULL);
		le_forceCloseConnection(connection);
	}

	connection->masks &= ~LE_QUEUE_READ;

	if( connection->masks & LE_READING ) {
		le_queueRead(connection, &connection->readReq);
	} else if( (connection->masks & LE_CLOSING) && (connection->pendingWriteReqs == 0) ) {
		le_connectionOver(connection);
	}
}

int le_write(le_TcpConnection* connection, le_WriteReq* req, le_Buffer bufs[], int bufCount, le_writeCB cb) {
	int result;
	DWORD bytes;

	if( !(connection->masks & LE_CONNECTION) ) {
		le_setErrorCode(connection->loop, WSAEINVAL);
		return LE_ERROR;
	}

	if( connection->masks & LE_SHUTDOWN_WRITE ) {
		le_setErrorCode(connection->loop, WSAESHUTDOWN);
		return LE_ERROR;
	}

	le_initWriteReq(connection, req, cb);
	memset(&req->overlapped, 0, sizeof(req->overlapped));

	result = WSASend(connection->socket,
					 (WSABUF*)bufs,
					 bufCount,
					 &bytes,
					 0,
					 &req->overlapped,
					 NULL);

	if( LE_SUCCEEDED_WITH_IOCP(result == ERROR_SUCCESS) ) {
		connection->pendingWriteReqs++;
	} else {
		le_setErrorCode(connection->loop, WSAGetLastError());
		return LE_ERROR;
	}

	return LE_OK;
}

static inline void le_processWriteReq(le_TcpConnection* connection, le_WriteReq* req) {
	if( LE_NT_SUCCESS(req) ) {
		req->writeCB(req, LE_GET_WRITE_BYTES(req));
	} else {
		le_setErrorCodeByNtStatus(connection->loop, req);
		req->writeCB(req, LE_ERROR);
		le_forceCloseConnection(connection);
	}

	connection->pendingWriteReqs--;
	if( (connection->masks & LE_CLOSING) &&
		!(connection->masks & LE_QUEUE_READ) &&
		(connection->pendingWriteReqs == 0) ) {
		le_connectionOver(connection);
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

int le_channelInit(le_EventLoop* loop, le_Channel* channel, le_channelCB channelCB, le_channelCloseCB closeCB) {
	assert(channelCB);

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

static inline void le_processChannelReq(le_EventLoop* loop, le_ChannelReq* req) {
	le_Queue* channelNode;
	le_Queue activityChannels;
	le_Channel* activityChannel;

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

int le_channelPost(le_Channel* channel) {
	if( channel->masks & LE_CLOSING ) {
		return LE_ERROR;
	}

	if( LE_ACCESS_ONCE(long, channel->pending) == 1 ) { // do simply check and stop compiler from optimizing
		return LE_OK;
	}

	if( InterlockedExchange(&channel->pending, 1) == 0 ) {
		le_EventLoop* loop = channel->loop;

		le_safeQueueAdd(&loop->pendingChannels, &channel->pendingNode);

		if( LE_ACCESS_ONCE(long, loop->posting) == 1 ) { // do simply check and stop compiler from optimizing
			return LE_OK;
		}

		if( InterlockedExchange(&loop->posting, 1) == 1 ) {
			return LE_OK;
		}

		if( !PostQueuedCompletionStatus(loop->iocp, 0, 0, &loop->channelReq.overlapped) ) { // wake up loop
			le_abort("PostQueuedCompletionStatus fail!(error no: %d)\n", GetLastError());
		}
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

static inline void le_processReqs(le_EventLoop* loop, le_BaseReq* req) {
	if( req->type == LE_READ ) {
		le_processReadReq(((le_ReadReq*)req)->connection, (le_ReadReq*)req);
	} else if( req->type == LE_WRITE ) {
		le_processWriteReq(((le_WriteReq*)req)->connection, (le_WriteReq*)req);
	} else if( req->type == LE_ACCEPT ) {
		le_processAcceptReq(loop->server, (le_AcceptReq*)req);
	} else if( req->type == LE_CONNECT ) {
		le_processConnectReq(((le_ConnectReq*)req)->connection, (le_ConnectReq*)req);
	} else if( req->type == LE_POST ) {
		le_processChannelReq(loop, (le_ChannelReq*)req);
	} else {
		assert(0);
	}
}

static void le_poll(le_EventLoop* loop, le_time_t timeout) {
	BOOL result;
	ULONG count;
	LPOVERLAPPED_ENTRY entrys = loop->overlappeds;

	result = GetQueuedCompletionStatus(loop->iocp,
									   &entrys->dwNumberOfBytesTransferred,
									   &entrys->lpCompletionKey,
									   &entrys->lpOverlapped,
									   (DWORD)timeout);

	if( entrys->lpOverlapped ) {
		le_processReqs(loop, (le_BaseReq*)entrys->lpOverlapped);

		for(count = 1; count < loop->maxOverlappeds; ++count) {
			GetQueuedCompletionStatus(loop->iocp,
									  &entrys->dwNumberOfBytesTransferred,
									  &entrys->lpCompletionKey,
									  &entrys->lpOverlapped,
									  0);

			if( !entrys->lpOverlapped ) {
				break;
			}

			le_processReqs(loop, (le_BaseReq*)entrys->lpOverlapped);
		}

		if( (count == loop->maxOverlappeds) && (loop->maxOverlappeds < LE_EVENTS_LIMIT) ) {
			loop->maxOverlappeds <<= 1;
		}
	}
}

static void le_pollEx(le_EventLoop* loop, le_time_t timeout) {
	BOOL result;
	unsigned i;
	ULONG count;
	LPOVERLAPPED_ENTRY entrys = loop->overlappeds;

	result = pGetQueuedCompletionStatusEx(loop->iocp,
										  entrys,
										  loop->maxOverlappeds,
										  &count,
										  (DWORD)timeout,
										  FALSE);

	if( result ) {
		for(i = 0; i < count; ++i) {
			le_processReqs(loop, (le_BaseReq*)entrys[i].lpOverlapped);
		}

		if( (count == loop->maxOverlappeds) && (loop->maxOverlappeds < LE_EVENTS_LIMIT) ) {
			loop->maxOverlappeds <<= 1;
			loop->overlappeds = (LPOVERLAPPED_ENTRY)le_realloc(loop->overlappeds, loop->maxOverlappeds * sizeof(OVERLAPPED_ENTRY));
		}
	}
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
		le_queueRemove(node);
	}
}

int le_serverClose(le_TcpServer* server) {
	if( !(server->masks & LE_LISTENING) ) {
		le_setErrorCode(server->loop, WSAEINVAL);
		return LE_ERROR;
	}

	if( server->socket == INVALID_SOCKET ) {
		le_setErrorCode(server->loop, WSAENOTSOCK);
		return LE_ERROR;
	}

	server->masks &= ~LE_LISTENING;
	closesocket(server->socket);
	server->socket = INVALID_SOCKET;

	le_closeAllConnections(server);

	if( server->pendingAcceptReqs == 0 ) {
		le_serverOver(server);
	}

	return LE_OK;
}

int le_connectionShutdown(le_TcpConnection* connection) {
	if( !(connection->masks & LE_CONNECTION) ) {
		le_setErrorCode(connection->loop, WSAEINVAL);
		return LE_ERROR;
	}

	if( connection->masks & LE_SHUTDOWN_WRITE ) {
		le_setErrorCode(connection->loop, WSAESHUTDOWN);
		return LE_ERROR;
	}

	connection->masks |= LE_SHUTDOWN_WRITE;
	shutdown(connection->socket, SD_SEND);

	return LE_OK;
}

int le_connectionClose(le_TcpConnection* connection) {
	if( connection->masks & LE_CLOSING ) {
		le_setErrorCode(connection->loop, WSAEINVAL);
		return LE_ERROR;
	}

	if( connection->socket == INVALID_SOCKET ) {
		le_setErrorCode(connection->loop, WSAENOTSOCK);
		return LE_ERROR;
	}

	connection->masks |= LE_CLOSING;
	connection->masks |= LE_SHUTDOWN_WRITE;

	if( (connection->pendingWriteReqs == 0) && !(connection->masks & LE_QUEUE_READ) ) {
		le_connectionOver(connection);
	} else {
		closesocket(connection->socket);
		connection->socket = INVALID_SOCKET;
	}
	//} else if( !(connection->masks & LE_SHUTDOWN_WRITE) ) {
	//	connection->masks |= LE_SHUTDOWN_WRITE;
	//	shutdown(connection->socket, SD_SEND);
	//}

	return LE_OK;
}

int le_bind(le_TcpServer* server, const char * ip, int port) {
	struct sockaddr_in addr;

	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(ip);

	if( server->socket != INVALID_SOCKET ) {
		le_setErrorCode(server->loop, WSAEINVAL);
		return LE_ERROR;
	}

	server->socket = socket(AF_INET, SOCK_STREAM, 0);
	if( server->socket == INVALID_SOCKET ) {
		le_setErrorCode(server->loop, WSAGetLastError());
		return LE_ERROR;
	}

	if( le_associateWithIocp(server->loop, server->socket) == LE_ERROR ) {
		return LE_ERROR;
	}

	if( bind(server->socket, (struct sockaddr*)&addr, sizeof(struct sockaddr)) == SOCKET_ERROR ) {
		le_setErrorCode(server->loop, WSAGetLastError());
		closesocket(server->socket);
		return LE_ERROR;
	}

	return LE_OK;
}

int le_listen(le_TcpServer* server, int backlog) {
	int i;
	le_AcceptReq* req;
	le_EventLoop* loop = server->loop;
	const GUID wsaidAcceptex = WSAID_ACCEPTEX;

	if( server->socket == INVALID_SOCKET ) {
		le_setErrorCode(loop, WSAEINVAL);
		return LE_ERROR;
	}

	loop->acceptex = (LPFN_ACCEPTEX)le_getWinsockFuncEx(server->socket, wsaidAcceptex);
	if( loop->acceptex == NULL ) {
		le_setErrorCode(loop, WSAGetLastError());
		return LE_ERROR;
	}

	if( listen(server->socket, backlog) == SOCKET_ERROR ) {
		le_setErrorCode(loop, WSAGetLastError());
		return LE_ERROR;
	}

	server->masks |= LE_LISTENING;
	LE_INCREASE_EVENTS(loop);

	for(i = 0; i < LE_QUEUED_ACCEPTS_COUNT; ++i) {
		req = &server->queuedAccepts[i];
		le_initAcceptReq(server, req);
		if( le_queueAccept(server, req) == LE_ERROR ) {
			return LE_ERROR;
		}
	}

	return LE_OK;
}

extern void le_processTimers(le_EventLoop* loop);
static inline void le_processEvents(le_EventLoop* loop, void (*poll)(le_EventLoop*, le_time_t)) {
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
			timeout = INFINITE;
		}

		poll(loop, timeout);
	}
}

void le_run(le_EventLoop* loop) {
	if( pGetQueuedCompletionStatusEx ) {
		le_processEvents(loop, le_pollEx);
	} else {
		le_processEvents(loop, le_poll);
	}
}

static inline void le_winInit() {
	WSADATA wsaData;
	HMODULE kernel32Module;
	int err;

	kernel32Module = GetModuleHandleA("kernel32.dll");
	if( !kernel32Module ) {
		le_abort("get kernel32 module fail!(error no: %d)", GetLastError());
	}

	pGetQueuedCompletionStatusEx = (sGetQueuedCompletionStatusEx) GetProcAddress(
		kernel32Module,
		"GetQueuedCompletionStatusEx");

	err = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if( err != 0 ) {
		le_abort("init win socket fail!(error no: %d)", err);
	}
}

le_EventLoop* le_eventLoopCreate() {
	le_EventLoop* loop;

	if( InterlockedCompareExchange(&le_initFlag, 1, 0) == 0 ) {
		le_winInit(); // init windows api once
	}

	loop = (le_EventLoop*)le_malloc(sizeof(le_EventLoop));
	if( !loop ) {
		le_abort("create loop fail! out of memory!");
	}

	le_eventLoopInit(loop);

	return loop;
}

void le_eventLoopDelete(le_EventLoop* loop) {
	CloseHandle(loop->iocp);
	le_timerHeapFree(loop->timerHeap);
	le_safeQueueDestroy(&loop->pendingChannels);
	le_free(loop->overlappeds);
	le_free(loop);
}

#undef LE_NT_ERROR
#undef LE_NT_SUCCESS
#undef LE_GET_WRITE_BYTES
#undef LE_SUCCEEDED_WITH_IOCP
#undef le_setErrorCodeByNtStatus
#undef le_forceCloseConnection

