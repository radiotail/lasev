//////////////////////////////////////////////////////////////////////////////////////
// lasev - a lite asynchronous event lib
//
// Mail: radiotail86@gmail.com
// About the details of license, please read LICENSE
//////////////////////////////////////////////////////////////////////////////////////

#ifndef LE_LASEV_H_
#define LE_LASEV_H_

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
# include "le_win.h"
#elif defined(__linux__)
# include "le_linux.h"
#else
# error Dont support this platform!
#endif

#include "le_queue.h"
#include "le_safeQueue.h"

#define LE_OK 0
#define LE_ERROR -1

#define LE_EVENTS_LIMIT 4096

typedef unsigned long le_time_t;

struct le_Timer;
struct le_Channel;
struct le_WriteReq;
struct le_TcpServer;
struct le_TcpConnection;

////////////////////////////////////////////////////////////////////////////////////////
// callback function.

// read callback. bytes > 0, can read data from buf. bytes == 0, peer closed.
// bytes < 0, have an error.
typedef void (*le_readCB)(struct le_TcpConnection* connection, int bytes, char* buf);
// connect callback. status != 0, have an error.
typedef void (*le_connectCB)(struct le_TcpConnection* connection, int status);
// connect callback. status != 0, have an error.
typedef void (*le_allocCB)(struct le_TcpConnection* connection, struct le_Buffer*);
// write callback. bytes < 0, have an error.
typedef void (*le_writeCB)(struct le_WriteReq* req, int bytes);
// connection close callback.
typedef void (*le_connectionCloseCB)(struct le_TcpConnection* connection);
// server close callback.
typedef void (*le_serverCloseCB)(struct le_TcpServer* server);
// connection callback. status != 0, have an error.
typedef void (*le_connectionCB)(struct le_TcpServer* server, int status);
// timer callback.
typedef void (*le_timerCB)(struct le_Timer* timer);
// channel post callback. status != 0, have an error.
typedef void (*le_channelCB)(struct le_Channel* channel, int status);
// channel close callback.
typedef void (*le_channelCloseCB)(struct le_Channel* channel);

// returns the base address of an instance of a structure given the type of the 
// structure and the address of a field within the containing structure.
#define LE_CONTAINING_RECORD(ptr, type, field) \
	((type*) ((char*)(ptr) - ((char*) &((type*)0)->field)))


////////////////////////////////////////////////////////////////////////////////////////
// request struct.

// base request struct. this is a super class of others request struct.
typedef struct le_BaseReq
{
	LE_BASE_REQ_MEMBERS
} le_BaseReq;

// write request struct.
typedef struct le_WriteReq
{
	LE_BASE_REQ_MEMBERS
	LE_PLATFORM_WRITE_FIELDS
	void* data;
	struct le_TcpConnection* connection;
	le_writeCB writeCB;
} le_WriteReq;

// connect request struct.
typedef struct le_ConnectReq
{
	LE_BASE_REQ_MEMBERS
	struct le_TcpConnection* connection;
} le_ConnectReq;


////////////////////////////////////////////////////////////////////////////////////////
// event struct.

// server struct
typedef struct le_TcpServer
{
	LE_PLATFORM_SERVER_FIELDS
	void* data;
	unsigned masks;
	struct le_EventLoop* loop;
	le_serverCloseCB closeCB;
	le_connectionCB connectionCB;
} le_TcpServer;

// connection struct
typedef struct le_TcpConnection
{
	LE_PLATFORM_CONNECTION_FIELDS
	union {
		LE_CONNECTION_PRIVATE_FIELDS
		LE_CONNECTOR_PRIVATE_FIELDS
	};
	void* data;
	unsigned masks;
	struct le_EventLoop* loop;
	le_Queue connectionNode;
	unsigned pendingWriteReqs;
	le_connectionCloseCB closeCB;
} le_TcpConnection;

// timer struct
typedef struct le_Timer
{
	void* data;
	unsigned index;
	unsigned masks;
	le_time_t timeout;
	le_time_t repeat;
	struct le_EventLoop* loop;
	le_timerCB timerCB;
} le_Timer;

// channel struct.
typedef struct le_Channel
{
	void* data;
	unsigned masks;
	volatile long pending;
	struct le_EventLoop* loop;
	le_Queue channelNode;
	le_Queue pendingNode;
	le_channelCB channelCB;
	le_channelCloseCB closeCB;
} le_Channel;

////////////////////////////////////////////////////////////////////////////////////////
// event loop struct.

typedef struct le_EventLoop
{
	LE_PLATFORM_LOOP_FIELDS
	void* data;
	le_time_t time;
	int errorCode;
	unsigned eventsCount;
	le_Queue channelHead;      // channels queue
	le_Queue connectionsHead;  // connections queue
	volatile long posting;     // channel posting flag
	le_safeQueueHead pendingChannels; // pending channels queue
	struct le_TimerHeap* timerHeap;
	le_TcpServer* server;
} le_EventLoop;

// create event loop(none thread safe)
LE_EXTERN le_EventLoop* le_eventLoopCreate();
// delete event loop
LE_EXTERN void le_eventLoopDelete(le_EventLoop* loop);
// run event loop
LE_EXTERN void le_run(le_EventLoop* loop);

// return last error's number code
LE_EXTERN int le_getErrorCode(le_EventLoop* loop);
// return error's string description
LE_EXTERN const char* le_strerror(int err);

// 3rd libary(jemalloc, tcmalloc) can be used to replace these API
#define le_malloc malloc
#define le_free free
#define le_realloc realloc

////////////////////////////////////////////////////////////////////////////////////////
// socket option function..

// set tcp nodelay. enable = 1 open, 0 close
#define le_setTcpNoDelay(tcpEvent, enable) le_setPlatformTcpNoDelay(tcpEvent, enable)
// set tcp keepalive. enable = 1 open, 0 close
#define le_setTcpKeepAlive(tcpEvent, enable) le_setPlatformTcpKeepAlive(tcpEvent, enable)
// set tcp sendbuffer. size is buffer size
#define le_setTcpSendBuffer(tcpEvent, size) le_setPlatformTcpSendBuffer(tcpEvent, size)

// a new tcp server must init at first
LE_EXTERN void le_tcpServerInit(le_EventLoop* loop, le_TcpServer* server, le_connectionCB connectionCB, le_serverCloseCB closeCB);
// a new tcp connection must init at first
LE_EXTERN void le_tcpConnectionInit(le_EventLoop* loop, le_TcpConnection* connection, le_connectionCloseCB closeCB);

// return LE_OK on success, LE_ERROR on error
LE_EXTERN int le_listen(le_TcpServer* server, int backlog);
LE_EXTERN int le_bind(le_TcpServer* server, const char * addr, int port);
LE_EXTERN int le_accept(le_TcpServer* server, le_TcpConnection* connection);
LE_EXTERN int le_connect(le_TcpConnection* connection, const char* ip, int port, le_connectCB cb);
// start read data from socekt
LE_EXTERN int le_startRead(le_TcpConnection* connection, le_readCB readCB, le_allocCB allocCB);
// stop read data from socekt, but it don't close read side of a duplex connection,
// so can call le_startRead to restart read
LE_EXTERN int le_stopRead(le_TcpConnection* connection);
// write data to socekt, bufs is buffer array, bufCount is array length
LE_EXTERN int le_write(le_TcpConnection* connection, le_WriteReq* req, le_Buffer bufs[], int bufCount, le_writeCB cb);
// close write side of a duplex connection
LE_EXTERN int le_connectionShutdown(le_TcpConnection* connection);
// close connection
LE_EXTERN int le_connectionClose(le_TcpConnection* connection);
// close server
LE_EXTERN int le_serverClose(le_TcpServer* server);

// a new timer must init at first
LE_EXTERN void le_timerInit(le_EventLoop* loop, le_Timer* timer);
// start timer after timeout, then will repeat using the repeat value if it's nonzero.
// return LE_OK on success, LE_ERROR on error
LE_EXTERN int le_timerStart(le_Timer* timer, le_time_t timeout, le_time_t repeat, le_timerCB timerCB);
// close timer
LE_EXTERN int le_timerClose(le_Timer* timer);
// set repeat value
LE_EXTERN void le_timerSetRepeat(le_Timer* timer, le_time_t repeat);
// return repeat value
LE_EXTERN le_time_t le_timerGetRepeat(le_Timer* timer);
// milliseconds
#define le_getNowTime(loop) loop->time
#define le_sleep(milliseconds) Sleep(milliseconds)

// init channel
LE_EXTERN int le_channelInit(le_EventLoop* loop, le_Channel* channel, le_channelCB channelCB, le_channelCloseCB closeCB);
// wake up loop thread. it can be called from other threads
LE_EXTERN int le_channelPost(le_Channel* channel);
// close channel. must be called from loop thread
LE_EXTERN void le_channelClose(le_Channel* channel);


////////////////////////////////////////////////////////////////////////////////////////
// threads api like POSIX threads

typedef pthread_t le_pthread;
#define le_pthreadSelf() pthread_self()
#define le_pthreadCreate(thread, start_routine, arg) pthread_create((thread), NULL, start_routine, arg)
#define le_pthreadJoin(thread) pthread_join(thread, NULL)

typedef pthread_mutex_t le_mutex;
#define le_mutexInit(mutex) pthread_mutex_init(mutex, NULL)
#define le_mutexDestroy(mutex) pthread_mutex_destroy(mutex)
#define le_mutexLock(mutex) pthread_mutex_lock(mutex)
#define le_mutexUnlock(mutex) pthread_mutex_unlock(mutex)
#define le_mutexTrylock(mutex) pthread_mutex_trylock(mutex)

#undef LE_BASE_REQ_MEMBERS
#undef LE_CONNECTION_PRIVATE_FIELDS
#undef LE_CONNECTOR_PRIVATE_FIELDS
#undef LE_PLATFORM_WRITE_FIELDS
#undef LE_PLATFORM_SERVER_FIELDS
#undef LE_PLATFORM_CONNECTION_FIELDS
#undef LE_PLATFORM_LOOP_FIELDS

#ifdef __cplusplus
}
#endif

#endif

