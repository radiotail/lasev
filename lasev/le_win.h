//////////////////////////////////////////////////////////////////////////////////////
// Mail: radiotail86@gmail.com
// About the details of license, please read LICENSE
//////////////////////////////////////////////////////////////////////////////////////

#ifndef LE_WIN_H_
#define LE_WIN_H_

#include <WinSock2.h>
#include <MSWSock.h>
#include <process.h>

#if defined(LE_BUILD_AS_DLL)
# define LE_EXTERN __declspec(dllexport) // export dll
#elif defined(LE_USE_AS_DLL)
# define LE_EXTERN __declspec(dllimport) // import dll
#else
# define LE_EXTERN extern
#endif

#define LE_OVERLAPEDS_COUNT 128
#define LE_QUEUED_ACCEPTS_COUNT 32

typedef enum{
	LE_WRITE   = 1,
	LE_READ    = 2,
	LE_ACCEPT  = 3,
	LE_CONNECT = 4,
	LE_POST	   = 5,
} le_ReqType;

typedef struct le_Buffer
{
	ULONG len;
	char* base;
} le_Buffer;

#define LE_BASE_REQ_MEMBERS \
	OVERLAPPED overlapped;  \
	le_ReqType type;

typedef struct le_AcceptReq
{
	LE_BASE_REQ_MEMBERS
	SOCKET socket;
	char buffer[sizeof(struct sockaddr_storage) * 2 + 32];
} le_AcceptReq;

typedef struct le_ReadReq
{
	LE_BASE_REQ_MEMBERS
	struct le_TcpConnection* connection;
} le_ReadReq;

typedef struct le_ChannelReq
{
	LE_BASE_REQ_MEMBERS
	void* data;
	struct le_Channel* channel;
} le_ChannelReq;

#define LE_PLATFORM_WRITE_FIELDS

#define LE_PLATFORM_LOOP_FIELDS    \
	HANDLE iocp;				   \
	OVERLAPPED_ENTRY* overlappeds; \
	unsigned maxOverlappeds;	   \
	le_ChannelReq channelReq;	   \
	LPFN_ACCEPTEX acceptex;		   \
	LPFN_CONNECTEX connectex;

#define LE_PLATFORM_SERVER_FIELDS   \
	SOCKET socket;					\
	unsigned pendingAcceptReqs;     \
	le_AcceptReq* pendingAcceptReq; \
	le_AcceptReq queuedAccepts[LE_QUEUED_ACCEPTS_COUNT];

#define LE_PLATFORM_CONNECTION_FIELDS \
	SOCKET socket;

#define LE_CONNECTION_PRIVATE_FIELDS \
	struct {						 \
		le_ReadReq readReq;			 \
		le_readCB readCB;			 \
		le_allocCB allocCB;			 \
	};

#define LE_CONNECTOR_PRIVATE_FIELDS \
	struct {						\
		le_ConnectReq connectReq;	\
		le_connectCB connectCB;		\
	};

struct le_TcpConnection;

LE_EXTERN int le__setTcpNoDelay(struct le_EventLoop* loop, int socket, int enable);
LE_EXTERN int le__setTcpKeepAlive(struct le_EventLoop* loop, int socket, int enable);
LE_EXTERN int le__setTcpSendBuffer(struct le_EventLoop* loop, int socket, int size);

#define le_setPlatformTcpNoDelay(tcpEvent, enable) \
	le__setTcpNoDelay(tcpEvent->loop, tcpEvent->socket, enable)

#define le_setPlatformTcpKeepAlive(tcpEvent, enable) \
	le__setTcpKeepAlive(tcpEvent->loop, tcpEvent->socket, enable)

#define le_setPlatformTcpSendBuffer(tcpEvent, size) \
	le__setTcpSendBuffer(tcpEvent->loop, tcpEvent->socket, size)


// thread
typedef struct thread_params {
	void* (*func)(void* );
	void* arg;
} thread_params;

typedef unsigned pthread_t;
#define pthread_self() GetCurrentThreadId()
LE_EXTERN int pthread_create(pthread_t *thread, const void* unused, void* (*start_routine)(void*), void* arg);
LE_EXTERN int pthread_join(pthread_t thread, void** unused);

typedef CRITICAL_SECTION pthread_mutex_t;
#define pthread_mutex_init(a, b) InitializeCriticalSection((a))
#define pthread_mutex_destroy(a) DeleteCriticalSection((a))
#define pthread_mutex_lock EnterCriticalSection
#define pthread_mutex_unlock LeaveCriticalSection
#define pthread_mutex_trylock(a) (TryEnterCriticalSection((a)) ? 0 : 1)

#endif
