#include "osKernelPrivate.h"
#include "osKernelInternal.h"
#include "osBlocking.h"

volatile uint32_t osLastWaitQueueResult;

/* Stop before publishing a partial task transition on queue misuse.
 * The captured PRIMASK may be the internal critical-section mask here;
 * errors=WAIT_QUEUE distinguishes this from invalid caller-context faults. */
static _Noreturn void waitFault(osWaitResult result, uint32_t operation)
{
	osLastWaitQueueResult = (uint32_t)result;
	osPortBlockingFault(OS_BLOCK_ERROR_WAIT_QUEUE, operation,
	                    osPortGetExecutionContext());
}

/* Validate an owner address before dereferencing it. Task storage is a
 * contiguous static array; a node's owner must identify an initialized slot.
 * Integer address checks avoid ordering unrelated C pointers. */
static bool registeredThread(const tcbType *thread)
{
	uintptr_t address = (uintptr_t)thread;
	uintptr_t start = (uintptr_t)&tcbs[0];
	if (thread_count > OS_MAX_THREADS || address < start) return false;
	uintptr_t offset = address - start;
	return (offset % sizeof(tcbType) == 0U) &&
	       (offset / sizeof(tcbType) < thread_count);
}

bool osKernelHasRunnableCaller(void)
{
	return osKernelRunning && registeredThread(currentPt) &&
	       (currentPt->state == THREAD_READY);
}

void *osKernelCurrentThreadIdentity(void)
{
	return osKernelHasRunnableCaller() ? currentPt : NULL;
}

/* Caller holds the kernel critical section from semaphore Take.
 * Validate the task and queue before changing state. Duplicate insertion
 * fails in Push, preserving both the wait relationship and queue links. */
void osKernelBlockCurrentOnSemaphore(osSemaphoreCore *sem)
{
	if (!osKernelHasRunnableCaller() || currentPt->waitSem != NULL ||
	    currentPt->waitMutex != NULL ||
	    currentPt->waitNode.owner != currentPt)
	{
		waitFault(OS_WAIT_CORRUPT, OS_BLOCK_OPERATION_WAIT_ENQUEUE);
	}
	osWaitResult result = osWaitQueuePush(&sem->waiters, &currentPt->waitNode);
	if (result != OS_WAIT_OK)
	{
		waitFault(result, OS_BLOCK_OPERATION_WAIT_ENQUEUE);
	}
	currentPt->waitSem = sem;
	currentPt->state = THREAD_BLOCKED;
}

/* Select by FIFO membership, not TCB creation order. Validation checks the
 * oldest waiter's identity and state BEFORE unlinking it. Direct handoff
 * reserves the signal for that task, leaving semaphore value zero. */
bool osKernelUnblockOneOnSemaphore(osSemaphoreCore *sem)
{
	osWaitResult result = osWaitQueueValidate(&sem->waiters);
	if (result != OS_WAIT_OK)
	{
		waitFault(result, OS_BLOCK_OPERATION_WAIT_DEQUEUE);
	}
	osWaitNode *node = sem->waiters.head;
	if (node == NULL) return false;
	tcbType *thread = node->owner;
	if (!registeredThread(thread) || &thread->waitNode != node ||
	    thread->state != THREAD_BLOCKED || thread->waitSem != sem ||
	    thread->waitMutex != NULL)
	{
		waitFault(OS_WAIT_CORRUPT, OS_BLOCK_OPERATION_WAIT_DEQUEUE);
	}
	osWaitNode *removed;
	result = osWaitQueuePop(&sem->waiters, &removed);
	if (result != OS_WAIT_OK)
	{
		waitFault(result, OS_BLOCK_OPERATION_WAIT_DEQUEUE);
	}
	thread->waitSem = NULL;
	thread->state = THREAD_READY;
	return true;
}

void osKernelBlockCurrentOnMutex(osMutex *mutex)
{
	if (!osKernelHasRunnableCaller() || currentPt->waitSem != NULL ||
	    currentPt->waitMutex != NULL || currentPt->waitNode.owner != currentPt)
	{
		waitFault(OS_WAIT_CORRUPT, OS_BLOCK_OPERATION_WAIT_ENQUEUE);
	}
	/* An owned mutex must identify a different initialized task. */
	if (!registeredThread(mutex->owner) || mutex->owner == currentPt)
	{
		waitFault(OS_WAIT_CORRUPT, OS_BLOCK_OPERATION_WAIT_ENQUEUE);
	}
	osWaitResult result = osWaitQueuePush(&mutex->waiters, &currentPt->waitNode);
	if (result != OS_WAIT_OK)
	{
		waitFault(result, OS_BLOCK_OPERATION_WAIT_ENQUEUE);
	}
	currentPt->waitMutex = mutex;
	currentPt->state = THREAD_BLOCKED;
}

bool osKernelReleaseMutex(osMutex *mutex)
{
	if (!osKernelHasRunnableCaller() || mutex->owner != currentPt)
	{
		waitFault(OS_WAIT_CORRUPT, OS_BLOCK_OPERATION_WAIT_DEQUEUE);
	}
	osWaitResult result = osWaitQueueValidate(&mutex->waiters);
	if (result != OS_WAIT_OK)
	{
		waitFault(result, OS_BLOCK_OPERATION_WAIT_DEQUEUE);
	}
	osWaitNode *node = mutex->waiters.head;
	if (node == NULL)
	{
		mutex->owner = NULL;
		return false;
	}
	tcbType *thread = node->owner;
	if (!registeredThread(thread) || &thread->waitNode != node ||
	    thread->state != THREAD_BLOCKED || thread->waitMutex != mutex ||
	    thread->waitSem != NULL || thread == currentPt)
	{
		waitFault(OS_WAIT_CORRUPT, OS_BLOCK_OPERATION_WAIT_DEQUEUE);
	}
	osWaitNode *removed;
	result = osWaitQueuePop(&mutex->waiters, &removed);
	if (result != OS_WAIT_OK)
	{
		waitFault(result, OS_BLOCK_OPERATION_WAIT_DEQUEUE);
	}
	/* Transfer BEFORE publishing READY. The waiter resumes as the owner. */
	mutex->owner = thread;
	thread->waitMutex = NULL;
	thread->state = THREAD_READY;
	return true;
}
