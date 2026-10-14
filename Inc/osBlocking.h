#ifndef OS_BLOCKING_H
#define OS_BLOCKING_H

#include "osPort.h"
#include "osKernelInternal.h"

/* Bit flags permit reporting several invalid caller conditions together. */
#define OS_BLOCK_ERROR_ISR       (1U << 0)
#define OS_BLOCK_ERROR_PRIMASK   (1U << 1)
#define OS_BLOCK_ERROR_BASEPRI   (1U << 2)
#define OS_BLOCK_ERROR_FAULTMASK (1U << 3)
#define OS_BLOCK_ERROR_NO_TASK   (1U << 4)
#define OS_BLOCK_ERROR_WAIT_QUEUE (1U << 5)
#define OS_BLOCK_ERROR_MUTEX_RECURSION (1U << 6)

#define OS_BLOCK_OPERATION_SEMAPHORE 1U
#define OS_BLOCK_OPERATION_DELAY     2U
#define OS_BLOCK_OPERATION_WAIT_ENQUEUE 3U
#define OS_BLOCK_OPERATION_WAIT_DEQUEUE 4U
#define OS_BLOCK_OPERATION_COUNT_SEMAPHORE 5U
#define OS_BLOCK_OPERATION_MUTEX_TAKE 6U
#define OS_BLOCK_OPERATION_MUTEX_GIVE 7U

/*
 * Shared policy for semaphore and delay. Evaluate a caller snapshot taken
 * BEFORE disabling interrupts internally; otherwise every valid call would
 * appear to have PRIMASK set. Any nonzero BASEPRI is rejected conservatively.
 * The caller must be a ready thread in a running kernel, not an ISR or a
 * task that has already published a blocked/sleeping state.
 *
 * Inline portable C keeps this policy testable on the host without adding
 * Cortex-M register access to synchronization code.
 */
static inline uint32_t osBlockingContextErrors(osExecutionContext caller)
{
	uint32_t errors = 0U;
	if (caller.ipsr != 0U) errors |= OS_BLOCK_ERROR_ISR;
	if (caller.primask != 0U) errors |= OS_BLOCK_ERROR_PRIMASK;
	if (caller.basepri != 0U) errors |= OS_BLOCK_ERROR_BASEPRI;
	if (caller.faultmask != 0U) errors |= OS_BLOCK_ERROR_FAULTMASK;
	if (!osKernelHasRunnableCaller()) errors |= OS_BLOCK_ERROR_NO_TASK;
	return errors;
}

#endif /* OS_BLOCKING_H */
