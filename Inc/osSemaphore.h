#ifndef OS_SEMAPHORE_H
#define OS_SEMAPHORE_H

#include <stdint.h>
#include "osWaitQueue.h"

/*-----------------------------------------------------------
 * osBinSem
 *
 * Represents a binary semaphore shared by kernel threads.
 * The value is restricted to zero (unavailable) or one
 * (available). It is declared volatile because both thread
 * code and kernel synchronization code may observe changes.
 *----------------------------------------------------------*/
typedef struct
{
	volatile int32_t value; /* Zero: unavailable, one: available. */
	osWaitQueue waiters;    /* FIFO order is local to this semaphore. */
} osBinSem;

/* Initializes before use. Never reinitialize while tasks are waiting. */
void osBinSemInit(osBinSem *sem, int32_t initial);

/* Takes a semaphore or blocks the calling thread until it is given. */
/* Task-only. Invalid blocking context is fatal; see osBlocking.h. */
void osBinSemTake(osBinSem *sem);

/* Gives a semaphore and wakes one waiting thread when present. */
void osBinSemGive(osBinSem *sem);

#endif /* OS_SEMAPHORE_H */
