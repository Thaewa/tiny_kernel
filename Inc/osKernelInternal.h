#ifndef OS_KERNEL_INTERNAL_H
#define OS_KERNEL_INTERNAL_H

#include <stdbool.h>

#include <osSemaphore.h>

/*
 * Marks the current thread as blocked on the supplied semaphore.
 * This is an internal service used by the synchronization subsystem.
 */
void osKernelBlockCurrentOnSemaphore(osBinSem *sem);

/*
 * Wakes one thread waiting on the supplied semaphore.
 * Returns true when a matching waiter was made ready.
 */
bool osKernelUnblockOneOnSemaphore(osBinSem *sem);

/* Requests that the scheduler select another runnable thread. */
void osKernelRequestContextSwitch(void);

#endif /* OS_KERNEL_INTERNAL_H */
