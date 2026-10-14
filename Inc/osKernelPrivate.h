#ifndef OS_KERNEL_PRIVATE_H
#define OS_KERNEL_PRIVATE_H

/* Kernel implementation details, not an application-facing task API.
 * Keeping TCB storage independent of MCU headers permits host tests of
 * the actual wait-state transitions, rather than a copy in a fake kernel. */
#include <stdint.h>
#include <stddef.h>
#include "osSemaphore.h"
#include "osMutex.h"

#define OS_MAX_THREADS 8U
_Static_assert(OS_MAX_THREADS <= OS_WAIT_QUEUE_MAX_NODES,
               "Wait queue capacity must cover kernel thread storage");

enum thread_state
{
	THREAD_READY = 0,
	THREAD_BLOCKED = 1,
	THREAD_SLEEPING = 2
};

typedef struct tcb
{
	int32_t *stackPt; /* Must remain first: PendSV assembly loads offset zero. */
	struct tcb *nextPt;
	enum thread_state state;
	osSemaphoreCore *waitSem; /* Points to the core of either semaphore type. */
	osMutex *waitMutex; /* Exactly one wait relationship may be active. */
	uint32_t wake_at_ms;
	osWaitNode waitNode; /* One intrusive node per task, shared by future waits. */
} tcbType;

_Static_assert(offsetof(tcbType, stackPt) == 0U,
               "PendSV requires stackPt at TCB offset zero");

extern tcbType tcbs[OS_MAX_THREADS];
extern tcbType *currentPt;
extern volatile uint8_t thread_count;
extern volatile uint32_t osKernelRunning;
extern volatile uint32_t osLastWaitQueueResult;

#endif /* OS_KERNEL_PRIVATE_H */
