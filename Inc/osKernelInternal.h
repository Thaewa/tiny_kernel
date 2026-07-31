#ifndef OS_KERNEL_INTERNAL_H
#define OS_KERNEL_INTERNAL_H

#include <osSemaphore.h>

/*
 * Marks the current thread as blocked on the supplied semaphore.
 * This is an internal service used by the synchronization subsystem.
 */
void osKernelBlockCurrentOnSemaphore(osBinSem *sem);

/* Requests that the scheduler select another runnable thread. */
void osKernelRequestContextSwitch(void);

#endif /* OS_KERNEL_INTERNAL_H */
