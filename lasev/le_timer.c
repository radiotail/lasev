//////////////////////////////////////////////////////////////////////////////////////
// Mail: radiotail86@gmail.com
// About the details of license, please read LICENSE
//////////////////////////////////////////////////////////////////////////////////////

#include "lasev.h"
#include "le_internal.h"
#include "le_timerHeap.h"

static inline void le_timerOver(le_Timer* timer) {
	LE_DECREASE_EVENTS(timer->loop);
	timer->masks &= ~LE_TIMER_STARTED;
}

void le_timerInit(le_EventLoop* loop, le_Timer* timer) {
	timer->loop = loop;
	
	timer->masks = 0;
	timer->index = 0;
	timer->timeout = 0;
	timer->repeat = 0;
	timer->timerCB = NULL;
}

void le_timerSetRepeat(le_Timer* timer, le_time_t repeat) {
	timer->repeat = repeat;
}

le_time_t le_timerGetRepeat(le_Timer* timer) {
	return timer->repeat;
}

int le_timerStart(le_Timer* timer, le_time_t timeout, le_time_t repeat, le_timerCB timerCB) {
	le_EventLoop* loop = timer->loop;

	if( timer->masks & LE_TIMER_STARTED ) {
		return LE_ERROR;
	}
	
	assert(timerCB);

	timer->timerCB = timerCB;
	timer->timeout = loop->time + timeout;
	timer->repeat = repeat;
	timer->masks |= LE_TIMER_STARTED;
	LE_INCREASE_EVENTS(loop);

	return le_timerHeapPush(loop->timerHeap, timer);;
}

int le_timerClose(le_Timer* timer) {
	le_EventLoop* loop = timer->loop;

	if( !(timer->masks & LE_TIMER_STARTED) ) {
		return LE_ERROR;
	}

	if( le_timerHeapRemove(loop->timerHeap, timer) == LE_ERROR ) {
		return LE_ERROR;
	}

	le_timerOver(timer);

	return LE_OK;
}

void le_processTimers(le_EventLoop* loop) {
	le_Timer* timer;
	
	while( (timer = le_timerHeapMin(loop->timerHeap)) && (timer->timeout <= loop->time) ) {
		le_timerHeapPop(loop->timerHeap);

		if( timer->repeat > 0 ) {
			timer->timeout += timer->repeat;
			if( le_timerHeapPush(loop->timerHeap, timer) == LE_ERROR ) {
				le_abort("push timer fail!");
			}
		} else {
			le_timerOver(timer);
		};

		timer->timerCB(timer);
	}
}

