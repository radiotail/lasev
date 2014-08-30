#ifndef LE_SAFE_QUEUE
#define LE_SAFE_QUEUE

#include "le_queue.h"

#ifdef _MSC_VER
# define inline __inline
#endif

typedef struct le_safeQueueHead
{
	pthread_mutex_t mutex;
	struct le_Queue* prev;
	struct le_Queue* next;
} le_safeQueueHead;

static inline void le_safeQueueInit(le_safeQueueHead* head) {
	pthread_mutex_init(head->mutex);
	le_queueInit(head);
}

static inline void le_safeQueueDestroy(le_safeQueueHead* head) {
	pthread_mutex_destroy(head->mutex);
}

static inline void le_safeQueueSwap(le_safeQueueHead* safeHead, le_Queue* head) {
	pthread_mutex_lock(safeHead->mutex);
	le_queueCopy(safeHead, head);
	le_queueInit(safeHead);
	pthread_mutex_unlock(safeHead->mutex);
}

static inline void le_safeQueueEmpty(le_safeQueueHead* head, le_Queue* node) {
	pthread_mutex_lock(head->mutex);
	le_queueEmpty(node);
	pthread_mutex_unlock(head->mutex);
}

static inline void le_safeQueueAdd(le_safeQueueHead* head, le_Queue* node) {
	pthread_mutex_lock(head->mutex);
	le_queueAdd(head, node);
	pthread_mutex_unlock(head->mutex);
}

static inline void le_safeQueueRemove(le_safeQueueHead* head, le_Queue* node) {
	pthread_mutex_lock(head->mutex);
	le_queueRemove(node);
	pthread_mutex_unlock(head->mutex);
}

#endif


