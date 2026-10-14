#ifndef OS_KERNEL_INTERNAL_H
#define OS_KERNEL_INTERNAL_H

#include <stdbool.h>

#include <osSemaphore.h>

/* True only after launch, with a current thread still in the ready state. */
bool osKernelHasRunnableCaller(void);

/*
 * Enqueues the current thread and publishes its blocked state atomically.
 * Caller must hold the kernel critical section.
 */
void osKernelBlockCurrentOnSemaphore(osSemaphoreCore *sem);

/*
 * Removes the oldest waiter and makes it ready. Caller holds the critical
 * section. Returns false only for an empty queue; corruption is fatal.
 */
bool osKernelUnblockOneOnSemaphore(osSemaphoreCore *sem);

/* Requests that the scheduler select another runnable thread. */
void osKernelRequestContextSwitch(void);

#endif /* OS_KERNEL_INTERNAL_H */
