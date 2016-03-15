//////////////////////////////////////////////////////////////////////////////////////
// Mail: radiotail86@gmail.com
// About the details of license, please read LICENSE
//////////////////////////////////////////////////////////////////////////////////////

#ifndef LE_SAFE_QUEUE_H_
#define LE_SAFE_QUEUE_H_

#include "le_queue.h"

#ifdef _MSC_VER
# define inline __inline
#endif

typedef struct le_safeQueueHead
{
	pthread_mutex_t mutex;
	struct le_Queue queue;
} le_safeQueueHead;

static inline void le_safeQueueInit(le_safeQueueHead* head) {
	pthread_mutex_init(&head->mutex, NULL);
	le_queueInit(&head->queue);
}

static inline void le_safeQueueDestroy(le_safeQueueHead* head) {
	pthread_mutex_destroy(&head->mutex);
}

static inline void le_safeQueueSwap(le_safeQueueHead* safeHead, le_Queue* head) {
	pthread_mutex_lock(&safeHead->mutex);
	if( le_queueEmpty(&safeHead->queue) ) {
		le_queueInit(head);
	} else {
		le_queueCopy(&safeHead->queue, head);
		head->prev->next = head;
		head->next->prev = head;
		le_queueInit(&safeHead->queue);
	}
	pthread_mutex_unlock(&safeHead->mutex);
}

static inline void le_safeQueueEmpty(le_safeQueueHead* head, le_Queue* node) {
	pthread_mutex_lock(&head->mutex);
	le_queueEmpty(node);
	pthread_mutex_unlock(&head->mutex);
}

static inline void le_safeQueueAdd(le_safeQueueHead* head, le_Queue* node) {
	pthread_mutex_lock(&head->mutex);
	le_queueAdd(&head->queue, node);
	pthread_mutex_unlock(&head->mutex);
}

static inline void le_safeQueueRemove(le_safeQueueHead* head, le_Queue* node) {
	pthread_mutex_lock(&head->mutex);
	le_queueRemove(node);
	pthread_mutex_unlock(&head->mutex);
}

#endif


