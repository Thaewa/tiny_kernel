#ifndef OS_PORT_H
#define OS_PORT_H

#include <stdint.h>

/* Saved interrupt-mask state returned by a kernel critical section. */
typedef uint32_t osIrqState;

/* Capture the caller before the kernel enters its own critical section. */
typedef struct
{
	uint32_t ipsr;
	uint32_t primask;
	uint32_t basepri;
	uint32_t faultmask;
} osExecutionContext;

typedef struct
{
	uint32_t errors;
	uint32_t operation; /* 1: binary take, 2: delay, 3/4: queue, 5: counting take. */
	osExecutionContext caller;
} osBlockingFaultRecord;

/* Debugger-visible diagnostics remain available after a fatal misuse. */
extern volatile osBlockingFaultRecord osLastBlockingFault;
osExecutionContext osPortGetExecutionContext(void);

/* Never returns as though a rejected void blocking API had succeeded. */
_Noreturn void osPortBlockingFault(uint32_t errors, uint32_t operation,
                                 osExecutionContext caller);

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
