#ifndef LE_TIMER_HEAP_H_
#define LE_TIMER_HEAP_H_

#include "lasev.h"
#include "le_internal.h"

#define LE_TIMER_CMP(a, b) ((a)->timeout < (b)->timeout)

#define LE_HEAP_PARENT(node) (node) >> 1
#define LE_HEAP_LEFT(node) (node) << 1
#define LE_HEAP_RIGHT(node) ((node) << 1) + 1

typedef struct le_TimerHeap
{
	unsigned size;
	unsigned capacity;
	le_Timer** data;
} le_TimerHeap;

static inline le_TimerHeap* le_timerHeapNew(unsigned size) {
	le_TimerHeap* heap = le_malloc(sizeof(le_TimerHeap));
	if( heap == NULL ) {
		return NULL;
	}

	size = size < 1? 1: size;
	heap->data = le_malloc((size + 1) * sizeof(le_Timer*));
	if( heap->data == NULL ) {
		le_free(heap);
		return NULL;
	}

	heap->size = 0;
	heap->capacity = size;
	heap->data[1] = NULL;

	return heap;
}

static inline void le_timerHeapFree(le_TimerHeap* timerHeap) {
	le_free(timerHeap->data);
	le_free(timerHeap);
}

static inline void le_timerHeapPrint(le_TimerHeap* timerHeap) {
	unsigned i;
	printf("\n<---------------------PRINT TIMER HEAP--------------------->\n");
	for(i = 1; i <= timerHeap->size; ++i) {
		printf("[%d]%lu ", i, timerHeap->data[i]->timeout);
	}
	printf("\n<---------------------PRINT OK--------------------->\n");
}

static inline void le_timerHeapShiftUp(le_TimerHeap* timerHeap, le_Timer* timer, unsigned node) {
	unsigned parent = LE_HEAP_PARENT(node);

	while( (node > 1) && LE_TIMER_CMP(timer, timerHeap->data[parent]) ) {
		timerHeap->data[node] = timerHeap->data[parent];
		timerHeap->data[parent]->index = node;
		node = parent;
		parent = LE_HEAP_PARENT(node);
	}

	timerHeap->data[node] = timer;
	timer->index = node;
}

static inline void le_timerHeapShiftDown(le_TimerHeap* timerHeap, le_Timer* timer, unsigned node) {
	unsigned child = LE_HEAP_LEFT(node);

	while( child < timerHeap->size ) {
		if( LE_TIMER_CMP(timerHeap->data[child + 1], timerHeap->data[child]) ) {
			++child;
		}

		if( LE_TIMER_CMP(timerHeap->data[child], timerHeap->data[timerHeap->size]) ) {
			timerHeap->data[node] = timerHeap->data[child];
			timerHeap->data[child]->index = node;
			node = child;
			child = LE_HEAP_LEFT(child);
		} else {
			break;
		}
	}

	timerHeap->data[node] = timerHeap->data[timerHeap->size];
	timerHeap->data[timerHeap->size--] = NULL;
}

static inline le_Timer* le_timerHeapMin(le_TimerHeap* timerHeap) {
	return timerHeap->data[1];
}

static inline int le_timerHeapPush(le_TimerHeap* timerHeap, le_Timer* timer) {
	if( ++timerHeap->size > timerHeap->capacity ) {
		timerHeap->capacity = timerHeap->capacity << 1;
		timerHeap->data = le_realloc(timerHeap->data, sizeof(le_Timer*) * (timerHeap->capacity + 1));
		if( timerHeap->data == NULL ) {
			return LE_ERROR;
		}
	}

	le_timerHeapShiftUp(timerHeap, timer, timerHeap->size);

	return LE_OK;
}

#define le_timerHeapPop(timerHeap) le_timerHeapShiftDown((timerHeap), (timerHeap)->data[1], 1)

static inline int le_timerHeapRemove(le_TimerHeap* timerHeap, le_Timer* timer) {
	le_Timer* last;
	unsigned parent;

	if( timer->index == 0 ) {
		return LE_ERROR;
	}

	last = timerHeap->data[timerHeap->size];
	parent = LE_HEAP_PARENT(timer->index);

	if( (timer->index > 1) && LE_TIMER_CMP(last, timerHeap->data[parent]) ) {
		le_timerHeapShiftUp(timerHeap, last, timer->index);
	} else {
		le_timerHeapShiftDown(timerHeap, last, timer->index);
	}

	return LE_OK;
}

#endif
