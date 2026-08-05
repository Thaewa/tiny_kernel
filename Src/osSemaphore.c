#include "osSemaphore.h"
#include "osKernelInternal.h"
#include "osPort.h"

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
}

/*-----------------------------------------------------------
 * osBinSemTake
 *
 * Acquires a binary semaphore for the calling thread. Shared
 * semaphore and thread state is protected by a kernel critical
 * section so a concurrent thread or interrupt cannot observe a
 * partially completed operation.
 *
 * If the semaphore is available, the signal is consumed and the
 * function returns immediately. If it is unavailable, the kernel
 * marks the current thread as blocked before a context switch is
 * requested.
 *
 * The port layer hides Cortex-M interrupt and memory-barrier
 * instructions from this portable synchronization module. Host
 * tests replace that layer with fakes that record each operation.
 *----------------------------------------------------------*/
void osBinSemTake(osBinSem *sem)
{
	/* Preserve the caller's interrupt-mask state before entering. */
	osIrqState irqState = osPortEnterCritical();

	/* Consume an available binary signal without blocking. */
	if (sem->value > 0)
	{
		sem->value = 0;

		/* Publish the semaphore update before leaving the section. */
		osPortMemoryBarrier();
		osPortExitCritical(irqState);
		return;
	}

	/* Record the wait relationship while kernel state is protected. */
	osKernelBlockCurrentOnSemaphore(sem);

	/* Publish the blocked state before scheduling another thread. */
	osPortMemoryBarrier();
	osPortExitCritical(irqState);

	/* Context switching is requested only after interrupts are restored. */
	osKernelRequestContextSwitch();
}

/*-----------------------------------------------------------
 * osBinSemGive
 *
 * Releases a binary semaphore inside a protected critical
 * section. When a waiter exists, the signal is handed directly
 * to exactly one blocked thread and the stored semaphore value
 * remains zero. Without a waiter, the signal is stored as one.
 * Repeated gives therefore coalesce instead of growing a count.
 *
 * A context switch is requested only for direct handoff so the
 * newly readied thread can participate in scheduling promptly.
 *----------------------------------------------------------*/
void osBinSemGive(osBinSem *sem)
{
	osIrqState irqState = osPortEnterCritical();
	bool waiterUnblocked = osKernelUnblockOneOnSemaphore(sem);

	if (!waiterUnblocked)
	{
		/* A binary semaphore stores at most one pending signal. */
		sem->value = 1;
	}

	/* Publish either the stored signal or the direct handoff state. */
	osPortMemoryBarrier();
	osPortExitCritical(irqState);

	if (waiterUnblocked)
	{
		osKernelRequestContextSwitch();
	}
}
