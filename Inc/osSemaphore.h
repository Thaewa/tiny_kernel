#ifndef OS_SEMAPHORE_H
#define OS_SEMAPHORE_H

#include <stdint.h>
#include <stdbool.h>
#include "osWaitQueue.h"

/*-----------------------------------------------------------
 * osSemaphoreCore
 *
 * Shared count and FIFO waiter storage. Binary and counting APIs
 * use the same wait-state machinery without making the kernel
 * know each semaphore's capacity policy. Applications should use
 * the public APIs, not call binary APIs on a counting core.
 *----------------------------------------------------------*/
typedef struct
{
	volatile int32_t value; /* Number of stored tokens; zero requires a wait. */
	osWaitQueue waiters;    /* FIFO order is local to this semaphore. */
} osSemaphoreCore;

/* Source-compatible binary storage; its API restricts value to 0 or 1. */
typedef osSemaphoreCore osBinSem;

typedef struct
{
	osSemaphoreCore core;
	uint32_t maxCount; /* Initialized once; immutable while in use. */
} osCountSem;

/* Initializes before use. Never reinitialize while tasks are waiting. */
void osBinSemInit(osBinSem *sem, int32_t initial);

/* Takes a semaphore or blocks the calling thread until it is given. */
/* Task-only. Invalid blocking context is fatal; see osBlocking.h. */
void osBinSemTake(osBinSem *sem);

/* Gives a semaphore and wakes one waiting thread when present. */
void osBinSemGive(osBinSem *sem);

/*
 * Initialize fresh/inactive storage. maxCount must be 1..INT32_MAX and
 * initialCount <= maxCount. Returns false for NULL or invalid limits,
 * leaving supplied storage unchanged. Never reset an active wait queue.
 */
bool osCountSemInit(osCountSem *sem, uint32_t maxCount, uint32_t initialCount);

/* Consume exactly one token, or block in FIFO order at zero.
 * Task-only, with the same blocking-context rules as binary Take. */
void osCountSemTake(osCountSem *sem);

/* Hand off to one waiter, or store one token. Returns false at capacity
 * without increasing the count. Requires valid initialized storage. */
bool osCountSemGive(osCountSem *sem);

#endif /* OS_SEMAPHORE_H */
