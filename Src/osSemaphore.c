#include "osSemaphore.h"
#include "osKernelInternal.h"
#include "osPort.h"
#include "osBlocking.h"
#include <limits.h>
#include <stddef.h>

/*-----------------------------------------------------------
 * osBinSemInit
 *
 * Initializes a binary semaphore before it is used by any
 * thread. A binary semaphore can represent only two states:
 * available (one) and unavailable (zero).
 *
 * Any positive initial value is normalized to one. Zero and
 * all negative values are normalized to zero. Normalization
 * prevents an invalid count from entering the binary
 * semaphore state.
 *
 * Parameters:
 *   sem     - Pointer to the semaphore object to initialize.
 *   initial - Requested initial availability value.
 *
 * Precondition:
 *   sem must point to valid writable storage. Null-pointer
 *   handling will be defined by a later kernel-wide error
 *   handling milestone.
 *----------------------------------------------------------*/
void osBinSemInit(osBinSem *sem, int32_t initial)
{
	/* Clamp the requested value to the binary range of zero or one. */
	sem->value = (initial > 0) ? 1 : 0;
	/* Initializes fresh storage; callers must not reset an active wait queue. */
	(void)osWaitQueueInit(&sem->waiters);
}

/*-----------------------------------------------------------
 * takeSignal
 *
 * Acquires one binary/counting token for the calling thread. Shared
 * semaphore and thread state is protected by a kernel critical
 * section so a concurrent thread or interrupt cannot observe a
 * partially completed operation.
 *
 * If the semaphore is available, the signal is consumed and the
 * function returns immediately. If it is unavailable, the kernel
 * marks the current thread as blocked before a context switch is
 * requested.
 *
 * Take requires a ready task in a running kernel. Only a fast take
 * may execute with a caller mask set. An unavailable take checks
 * the original caller context before publishing any wait state;
 * misuse stops in osPortBlockingFault with debugger diagnostics.
 *
 * The port layer hides Cortex-M interrupt and memory-barrier
 * instructions from this portable synchronization module. Host
 * tests replace that layer with fakes that record each operation.
 *----------------------------------------------------------*/
static void takeSignal(osSemaphoreCore *sem, uint32_t operation)
{
	/* Inspect the caller, not the interrupt mask introduced below. */
	osExecutionContext caller = osPortGetExecutionContext();
	uint32_t errors = osBlockingContextErrors(caller);

	/* This is a task API even when a signal is immediately available. */
	if (errors & (OS_BLOCK_ERROR_ISR | OS_BLOCK_ERROR_NO_TASK))
	{
		osPortBlockingFault(errors, operation, caller);
	}

	/* Preserve the caller's interrupt-mask state before entering. */
	osIrqState irqState = osPortEnterCritical();

	/* Both variants consume ONE stored token. A counting semaphore may
	 * still have tokens afterward; binary storage starts at most at one. */
	if (sem->value > 0)
	{
		sem->value--;

		/* Publish the semaphore update before leaving the section. */
		osPortMemoryBarrier();
		osPortExitCritical(irqState);
		return;
	}

	/* Reject unsafe blocking before touching the task's wait relationship. */
	if (errors != 0U)
	{
		osPortExitCritical(irqState);
		osPortBlockingFault(errors, operation, caller);
	}

	/* Record the wait relationship while kernel state is protected. */
	osKernelBlockCurrentOnSemaphore(sem);

	/* Publish the blocked state before scheduling another thread. */
	osPortMemoryBarrier();

	/* Publish state and pend the switch as one protected transition.
	 * PendSV runs only after the saved interrupt state is restored. */
	osKernelRequestContextSwitch();
	osPortExitCritical(irqState);
}

void osBinSemTake(osBinSem *sem)
{
	takeSignal(sem, OS_BLOCK_OPERATION_SEMAPHORE);
}

/*-----------------------------------------------------------
 * giveSignal
 *
 * Releases one signal inside a protected critical
 * section. When a waiter exists, the signal is handed directly
 * to exactly one blocked thread and the stored semaphore value
 * remains zero. Without a waiter, store one token up to limit.
 * Binary calls coalesce at capacity; counting calls report false.
 *
 * A context switch is requested only for direct handoff so the
 * newly readied thread can participate in scheduling promptly.
 *----------------------------------------------------------*/
static bool giveSignal(osSemaphoreCore *sem, uint32_t limit, bool coalesce)
{
	osIrqState irqState = osPortEnterCritical();
	bool waiterUnblocked = osKernelUnblockOneOnSemaphore(sem);
	bool accepted = true;

	if (!waiterUnblocked)
	{
		if ((uint32_t)sem->value < limit)
		{
			/* Init bounds limit to INT32_MAX, so this increment cannot
			 * overflow the signed count in a valid semaphore. */
			sem->value++;
		}
		else
		{
			/* Binary gives coalesce; counting gives report capacity. */
			accepted = coalesce;
		}
	}

	/* Publish either the stored signal or the direct handoff state. */
	osPortMemoryBarrier();

	if (waiterUnblocked)
	{
		/* Keep the readiness update and scheduling request atomic. */
		osKernelRequestContextSwitch();
	}
	osPortExitCritical(irqState);
	return accepted;
}

void osBinSemGive(osBinSem *sem)
{
	/* Existing void API and signal-coalescing behavior remain unchanged. */
	(void)giveSignal(sem, 1U, true);
}

/*-----------------------------------------------------------
 * osCountSemInit
 *
 * Validate before writing any fields. A rejected initialization must
 * not turn existing storage into a half-initialized semaphore. Count
 * storage remains signed for compatibility with the binary core, so
 * the supported maximum is INT32_MAX rather than UINT32_MAX.
 *----------------------------------------------------------*/
bool osCountSemInit(osCountSem *sem, uint32_t maxCount, uint32_t initialCount)
{
	if (sem == NULL || maxCount == 0U || maxCount > (uint32_t)INT32_MAX ||
	    initialCount > maxCount)
	{
		return false;
	}
	sem->core.value = (int32_t)initialCount;
	(void)osWaitQueueInit(&sem->core.waiters);
	sem->maxCount = maxCount;
	return true;
}

void osCountSemTake(osCountSem *sem)
{
	takeSignal(&sem->core, OS_BLOCK_OPERATION_COUNT_SEMAPHORE);
}

bool osCountSemGive(osCountSem *sem)
{
	/* A blocked waiter receives the new token directly: count stays zero.
	 * Only a Give with no waiter consumes one slot of stored capacity. */
	return giveSignal(&sem->core, sem->maxCount, false);
}
