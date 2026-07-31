#ifndef OS_PORT_H
#define OS_PORT_H

#include <stdint.h>

/* Saved interrupt-mask state returned by a kernel critical section. */
typedef uint32_t osIrqState;

/*
 * Enters a critical section and returns the previous interrupt-mask state.
 * The returned state must be passed unchanged to osPortExitCritical().
 */
osIrqState osPortEnterCritical(void);

/* Restores the interrupt-mask state saved by osPortEnterCritical(). */
void osPortExitCritical(osIrqState state);

/* Completes outstanding memory accesses before shared state is observed. */
void osPortMemoryBarrier(void);

#endif /* OS_PORT_H */
