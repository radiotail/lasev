//////////////////////////////////////////////////////////////////////////////////////
// Mail: radiotail86@gmail.com
// About the details of license, please read LICENSE
//////////////////////////////////////////////////////////////////////////////////////

#ifndef LE_QUEUE_H_
#define LE_QUEUE_H_

typedef struct le_Queue
{
	struct le_Queue* prev;
	struct le_Queue* next;
} le_Queue;

#define le_queueInit(head) \
	(head)->prev = head;   \
	(head)->next = head

#define le_queueEmpty(node) \
	(node == (node)->prev)

#define le_queueCopy(srcHead, destHead) \
	(destHead)->prev = (srcHead)->prev;	\
	(destHead)->next = (srcHead)->next

#define le_queueNext(node) \
	(node)->next

#define le_queuePrev(node) \
	(node)->prev

#define le_queueAdd(head, node)  \
	(head)->prev->next = node;   \
	(node)->prev = (head)->prev; \
	(head)->prev = node;		 \
	(node)->next = head

#define le_queueRemove(node) \
	(node)->prev->next = (node)->next; \
	(node)->next->prev = (node)->prev

#define le_queueForeach(node, head)    \
	for( (node) = (head)->next;        \
	(node) != (head);                  \
	(node) = (node)->next )

#endif

