#ifndef OS_MUTEX_H
#define OS_MUTEX_H

#include <stdbool.h>
#include "osWaitQueue.h"

/*
 * Non-recursive mutex with FIFO ownership handoff. owner is an opaque
 * kernel task identity, not an application-supplied pointer. Never change
 * owner or waiters directly. There is no priority inheritance yet.
 */
typedef struct osMutex
{
	void *owner; /* NULL means unlocked, otherwise identifies the owning TCB. */
	osWaitQueue waiters;
} osMutex;

/* Initialize fresh/inactive storage before use. Never reset a held mutex
 * or one with waiters. All APIs require live, initialized non-NULL storage. */
void osMutexInit(osMutex *mutex);

/* Task-only: acquire immediately or block in FIFO order. Taking a mutex
 * already owned by the caller is fatal in this non-recursive version.
 * Only an immediate acquisition may occur while scheduling is masked. */
void osMutexTake(osMutex *mutex);

/* Task-only. Returns false if unlocked or caller is not the owner.
 * A successful Give transfers ownership to the oldest waiter atomically,
 * or unlocks when nobody waits. It never blocks the releasing task. */
bool osMutexGive(osMutex *mutex);

#endif /* OS_MUTEX_H */
