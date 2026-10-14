#include "osMutex.h"
#include "osBlocking.h"
#include <stddef.h>

void osMutexInit(osMutex *mutex)
{
	mutex->owner = NULL;
	(void)osWaitQueueInit(&mutex->waiters);
}

/*-----------------------------------------------------------
 * osMutexTake
 *
 * Snapshot caller masks before entering the internal critical section.
 * Ownership and queue changes are protected together. Free acquisition
 * records the caller as owner; it does not consume a semaphore signal.
 * Contention enqueues this task and requests PendSV before unlocking the
 * critical section. When it eventually resumes, ownership is already its
 * own: the blocked path must NOT take again and accidentally block itself.
 *----------------------------------------------------------*/
void osMutexTake(osMutex *mutex)
{
	osExecutionContext caller = osPortGetExecutionContext();
	uint32_t errors = osBlockingContextErrors(caller);
	if (errors & (OS_BLOCK_ERROR_ISR | OS_BLOCK_ERROR_NO_TASK))
	{
		osPortBlockingFault(errors, OS_BLOCK_OPERATION_MUTEX_TAKE, caller);
	}
	osIrqState irqState = osPortEnterCritical();
	void *identity = osKernelCurrentThreadIdentity();

	if (mutex->owner == identity)
	{
		/* Reject self-deadlock before changing either owner or wait links. */
		osPortExitCritical(irqState);
		osPortBlockingFault(OS_BLOCK_ERROR_MUTEX_RECURSION,
		                    OS_BLOCK_OPERATION_MUTEX_TAKE, caller);
	}
	if (mutex->owner == NULL)
	{
		/* A free mutex must not have orphaned waiters. */
		if (osWaitQueueValidate(&mutex->waiters) != OS_WAIT_OK ||
		    mutex->waiters.count != 0U)
		{
			osPortBlockingFault(OS_BLOCK_ERROR_WAIT_QUEUE,
			                    OS_BLOCK_OPERATION_MUTEX_TAKE, caller);
		}
		mutex->owner = identity;
		osPortMemoryBarrier();
		osPortExitCritical(irqState);
		return;
	}
	if (errors != 0U)
	{
		osPortExitCritical(irqState);
		osPortBlockingFault(errors, OS_BLOCK_OPERATION_MUTEX_TAKE, caller);
	}
	osKernelBlockCurrentOnMutex(mutex);
	osPortMemoryBarrier();
	osKernelRequestContextSwitch();
	osPortExitCritical(irqState);
}

/*-----------------------------------------------------------
 * osMutexGive
 *
 * Semaphore Give has no ownership restriction; mutex Give does.
 * Check identity before any queue or state mutation. The kernel helper
 * validates the oldest waiter, detaches its node, assigns owner, and only
 * then marks it ready. This avoids an unlocked gap that could let another
 * task steal the mutex before the selected waiter actually runs.
 *----------------------------------------------------------*/
bool osMutexGive(osMutex *mutex)
{
	osExecutionContext caller = osPortGetExecutionContext();
	uint32_t errors = osBlockingContextErrors(caller);
	if (errors & (OS_BLOCK_ERROR_ISR | OS_BLOCK_ERROR_NO_TASK))
	{
		osPortBlockingFault(errors, OS_BLOCK_OPERATION_MUTEX_GIVE, caller);
	}
	osIrqState irqState = osPortEnterCritical();
	if (mutex->owner != osKernelCurrentThreadIdentity())
	{
		osPortExitCritical(irqState);
		return false;
	}
	bool handedOff = osKernelReleaseMutex(mutex);
	osPortMemoryBarrier();
	if (handedOff) osKernelRequestContextSwitch();
	osPortExitCritical(irqState);
	return true;
}
