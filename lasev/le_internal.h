#ifndef LE_INTERNAL_H_
#define LE_INTERNAL_H_

#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>

#ifdef _MSC_VER
# define inline __inline
#endif

static inline void le_abort(const char* fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
	fprintf(stderr, fmt, ap);
    va_end(ap);

	abort();
}

static inline void le_setErrorCode(le_EventLoop* loop, int errorCode) {
	loop->errorCode = errorCode;
}

#define LE_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define LE_INCREASE_EVENTS(loop) (loop)->eventsCount++
#define LE_DECREASE_EVENTS(loop) (loop)->eventsCount--

#define LE_EVENTS_LIMIT 4096

#define LE_LISTENING			0x00000001
#define LE_CONNECTION			0x00000002
#define LE_READING				0x00000004
#define LE_CONNECTING			0x00000008
#define LE_QUEUE_READ			0x00000010
#define LE_QUEUED_ACCEPT		0x00000020
#define LE_SHUTDOWN_WRITE		0x00000040
#define LE_CLOSING				0x00000080
#define LE_TIMER_STARTED		0x00000100
#define LE_CHANNEL_WORKING		0x00000200


#endif
