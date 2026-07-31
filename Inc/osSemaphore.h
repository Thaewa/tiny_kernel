#ifndef OS_SEMAPHORE_H
#define OS_SEMAPHORE_H

#include <stdint.h>

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
} osBinSem;

/* Initializes a binary semaphore before the scheduler starts. */
void osBinSemInit(osBinSem *sem, int32_t initial);

/* Takes a semaphore or blocks the calling thread until it is given. */
void osBinSemTake(osBinSem *sem);

/* Gives a semaphore and wakes one waiting thread when present. */
void osBinSemGive(osBinSem *sem);

#endif /* OS_SEMAPHORE_H */
