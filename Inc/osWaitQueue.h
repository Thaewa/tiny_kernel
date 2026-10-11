#ifndef OS_WAIT_QUEUE_H
#define OS_WAIT_QUEUE_H

#include <stdint.h>

/* Match the current kernel's eight-thread limit, including its idle thread.
 * Keep validation bounded even when a damaged list contains a cycle. */
#define OS_WAIT_QUEUE_MAX_NODES 8U

typedef struct osWaitQueue osWaitQueue;
typedef struct osWaitNode osWaitNode;

struct osWaitNode
{
	osWaitNode *next;
	osWaitNode *previous;
	osWaitQueue *container; /* NULL means this node is not waiting anywhere. */
	void *owner;            /* Caller-owned payload, later a pointer to a TCB. */
};

struct osWaitQueue
{
	osWaitNode *head; /* Oldest waiter; removed first. */
	osWaitNode *tail; /* Newest waiter; new nodes append here. */
	uint32_t count;
};

typedef enum
{
	OS_WAIT_OK = 0,
	OS_WAIT_EMPTY,
	OS_WAIT_ALREADY_LINKED,
	OS_WAIT_NOT_MEMBER,
	OS_WAIT_FULL,
	OS_WAIT_INVALID_ARGUMENT,
	OS_WAIT_CORRUPT
} osWaitResult;

/*
 * Initialize once before first use. Reinitialization is permitted only for
 * an empty queue or an unlinked node; it is not an unlink operation.
 * The caller owns storage and must keep queues/nodes/owner payloads alive.
 */
osWaitResult osWaitQueueInit(osWaitQueue *queue);
osWaitResult osWaitNodeInit(osWaitNode *node, void *owner);

/*
 * All operations, including validation, require external serialization.
 * The kernel will hold its critical section around queue + task updates.
 * This module never disables interrupts or requests a context switch.
 * Pointers must refer to initialized, accessible objects: validation detects
 * logical corruption but cannot make arbitrary damaged addresses safe.
 */
osWaitResult osWaitQueueValidate(const osWaitQueue *queue);
osWaitResult osWaitQueuePush(osWaitQueue *queue, osWaitNode *node);

/* On error/empty, *node is NULL. Output storage must not alias queue links. */
osWaitResult osWaitQueuePop(osWaitQueue *queue, osWaitNode **node);

/* Remove a particular member, for future cancellation/timeout handling. */
osWaitResult osWaitQueueRemove(osWaitQueue *queue, osWaitNode *node);

#endif /* OS_WAIT_QUEUE_H */
