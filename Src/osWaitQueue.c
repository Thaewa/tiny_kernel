#include "osWaitQueue.h"
#include <stddef.h>

osWaitResult osWaitQueueInit(osWaitQueue *queue)
{
	if (queue == NULL) return OS_WAIT_INVALID_ARGUMENT;
	queue->head = NULL;
	queue->tail = NULL;
	queue->count = 0U;
	return OS_WAIT_OK;
}

osWaitResult osWaitNodeInit(osWaitNode *node, void *owner)
{
	if (node == NULL) return OS_WAIT_INVALID_ARGUMENT;
	node->next = NULL;
	node->previous = NULL;
	node->container = NULL;
	node->owner = owner;
	return OS_WAIT_OK;
}

/*-----------------------------------------------------------
 * osWaitQueueValidate
 *
 * Validate topology and membership before any mutating operation.
 * A NULL-terminated list has no sentinel or intentional self-links.
 * Walking at most eight nodes bounds the work independently of count:
 * a corrupted count cannot turn cycle detection into an endless loop.
 * Each previous link must identify the node actually visited last.
 * This also verifies next/previous consistency without a second walk.
 *----------------------------------------------------------*/
osWaitResult osWaitQueueValidate(const osWaitQueue *queue)
{
	if (queue == NULL) return OS_WAIT_INVALID_ARGUMENT;
	if (queue->count > OS_WAIT_QUEUE_MAX_NODES) return OS_WAIT_CORRUPT;
	if (queue->count == 0U)
	{
		return (queue->head == NULL && queue->tail == NULL)
		       ? OS_WAIT_OK : OS_WAIT_CORRUPT;
	}
	if (queue->head == NULL || queue->tail == NULL) return OS_WAIT_CORRUPT;
	if (queue->head->previous != NULL || queue->tail->next != NULL)
	{
		return OS_WAIT_CORRUPT;
	}

	const osWaitNode *node = queue->head;
	const osWaitNode *previous = NULL;
	uint32_t visited = 0U;
	while (node != NULL)
	{
		if (visited >= OS_WAIT_QUEUE_MAX_NODES || visited >= queue->count)
		{
			return OS_WAIT_CORRUPT;
		}
		if (node->container != queue || node->previous != previous ||
		    node->next == node || node->previous == node)
		{
			return OS_WAIT_CORRUPT;
		}
		previous = node;
		node = node->next;
		visited++;
	}
	return (visited == queue->count && previous == queue->tail)
	       ? OS_WAIT_OK : OS_WAIT_CORRUPT;
}

/*-----------------------------------------------------------
 * osWaitQueuePush
 *
 * Append to tail so earlier waiters retain FIFO order. Membership is
 * checked before writing links: inserting the same node into this or
 * another queue returns ALREADY_LINKED without altering either list.
 * Unlinked nodes must have cleared links, as init/pop/remove guarantee.
 * Validation intentionally costs O(n), bounded by eight nodes here;
 * actual insertion changes a constant number of pointers.
 *----------------------------------------------------------*/
osWaitResult osWaitQueuePush(osWaitQueue *queue, osWaitNode *node)
{
	if (queue == NULL || node == NULL) return OS_WAIT_INVALID_ARGUMENT;
	osWaitResult result = osWaitQueueValidate(queue);
	if (result != OS_WAIT_OK) return result;
	if (node->container != NULL) return OS_WAIT_ALREADY_LINKED;
	if (node->next != NULL || node->previous != NULL) return OS_WAIT_CORRUPT;
	if (queue->count == OS_WAIT_QUEUE_MAX_NODES) return OS_WAIT_FULL;

	node->previous = queue->tail;
	node->container = queue;
	if (queue->tail != NULL)
	{
		queue->tail->next = node;
	}
	else
	{
		/* First member becomes both head and tail. */
		queue->head = node;
	}
	queue->tail = node;
	queue->count++;
	return OS_WAIT_OK;
}

/* Called only after validation and proof of actual membership. */
static void unlinkNode(osWaitQueue *queue, osWaitNode *node)
{
	if (node->previous != NULL) node->previous->next = node->next;
	else queue->head = node->next;
	if (node->next != NULL) node->next->previous = node->previous;
	else queue->tail = node->previous;
	queue->count--;

	/* Clear membership and links together; owner survives for node reuse. */
	node->next = NULL;
	node->previous = NULL;
	node->container = NULL;
}

osWaitResult osWaitQueuePop(osWaitQueue *queue, osWaitNode **node)
{
	if (node == NULL) return OS_WAIT_INVALID_ARGUMENT;
	*node = NULL;
	osWaitResult result = osWaitQueueValidate(queue);
	if (result != OS_WAIT_OK) return result;
	if (queue->head == NULL) return OS_WAIT_EMPTY;

	osWaitNode *oldest = queue->head;
	unlinkNode(queue, oldest);
	*node = oldest;
	return OS_WAIT_OK;
}

osWaitResult osWaitQueueRemove(osWaitQueue *queue, osWaitNode *node)
{
	if (queue == NULL || node == NULL) return OS_WAIT_INVALID_ARGUMENT;
	osWaitResult result = osWaitQueueValidate(queue);
	if (result != OS_WAIT_OK) return result;
	if (node->container != queue) return OS_WAIT_NOT_MEMBER;

	/* A forged/stale container is not proof the node is actually linked.
	 * Validation has already bounded and checked this traversal. */
	for (osWaitNode *member = queue->head; member != NULL; member = member->next)
	{
		if (member == node)
		{
			unlinkNode(queue, node);
			return OS_WAIT_OK;
		}
	}
	return OS_WAIT_CORRUPT;
}
