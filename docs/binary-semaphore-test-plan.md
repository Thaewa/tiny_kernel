# Binary Semaphore Test Plan

## Purpose

This document defines the expected behavior and initial test coverage for the
Tiny Kernel binary semaphore. The first test milestone validates existing
behavior without adding counting semaphores, timeouts, task priorities, or
interrupt-service-routine APIs.

## Phase 1 Contract

### Initialization

- An initial value greater than zero makes the semaphore available.
- An initial value of zero or less makes the semaphore unavailable.
- A binary semaphore stores only the values zero and one.

### Take

- Taking an available semaphore changes its value from one to zero and returns
  immediately.
- Taking an unavailable semaphore changes the calling thread to the blocked
  state and records the semaphore on which it is waiting.
- A blocked thread must not be selected by the scheduler.
- After another thread gives the semaphore, the selected waiter resumes from
  its call to `osBinSemTake()`.

### Give

- Giving a semaphore with no waiters makes it available.
- Repeated gives with no waiters do not increase its value beyond one.
- Giving a semaphore with waiters wakes exactly one waiter.
- The current implementation uses direct handoff: when a waiter is woken, the
  semaphore remains unavailable because ownership of the signal is transferred
  directly to that waiter.
- Waking a waiter requests a reschedule.

### Waiter Selection

- Phase 1 does not guarantee FIFO or priority ordering.
- The implementation must wake only a thread blocked on the semaphore being
  given.
- A dedicated wait queue will define ordering in a later milestone.

## Out of Scope for Phase 1

- Calls from interrupt service routines
- Timed waits or non-blocking try-take operations
- Counting semaphores
- Mutex ownership
- Recursive locking
- Task priorities and priority inheritance
- Dynamic deletion of threads or semaphore objects
- Null-pointer recovery in release builds

Calling semaphore functions with a null pointer is programmer error. Assertion
and status-code behavior will be defined as part of the kernel-wide API error
handling milestone.

## Test Cases

| ID | Scenario | Expected result |
|---|---|---|
| BS-01 | Initialize with `1` | Semaphore value is `1` |
| BS-02 | Initialize with `0` | Semaphore value is `0` |
| BS-03 | Initialize with a positive value greater than `1` | Value is clamped to `1` |
| BS-04 | Initialize with a negative value | Value is clamped to `0` |
| BS-05 | Take when value is `1` | Value becomes `0`; caller remains ready |
| BS-06 | Take when value is `0` | Caller becomes blocked on this semaphore |
| BS-07 | Give when value is `0` and no waiter exists | Value becomes `1` |
| BS-08 | Give when value is `1` and no waiter exists | Value remains `1` |
| BS-09 | Give with one waiter | Exactly that waiter becomes ready |
| BS-10 | Give with multiple waiters | Exactly one matching waiter becomes ready |
| BS-11 | Give while threads wait on different semaphores | Only a waiter for the given semaphore wakes |
| BS-12 | Scheduler runs after a thread blocks | Blocked thread is skipped |
| BS-13 | All user threads are blocked | Idle thread remains runnable |
| BS-14 | Woken thread resumes | `osBinSemTake()` returns after direct handoff |
| BS-15 | Multiple gives with no waiter | Signal is coalesced and value remains `1` |

## Host-Test Boundary

The semaphore code currently depends directly on Cortex-M interrupt controls,
global task-control-block storage, and the context-switch request. To run these
tests without hardware, the minimum required test seam is:

```c
typedef uint32_t os_irq_state_t;

os_irq_state_t osPortEnterCritical(void);
void osPortExitCritical(os_irq_state_t state);
void osPortRequestContextSwitch(void);
```

Scheduler state transitions must also be callable without executing the
Cortex-M context-switch assembly. The host implementation will record context
switch requests, while the Cortex-M implementation will eventually pend
PendSV.

## Hardware Tests Deferred Until a Board Is Available

- Context save and restore across a blocking take
- Interrupt priority and nested-interrupt behavior
- Semaphore give from a future ISR-safe API
- Long-running timing and race-condition stress tests
- GPIO or logic-analyzer traces of task handoff latency
- Stack integrity during repeated block and wake cycles

## Exit Criteria

Phase 1 is complete when:

- Tests BS-01 through BS-15 pass in the host test environment where applicable.
- The firmware still builds for STM32F411RE.
- No public semaphore behavior changes without an update to this contract.
- Hardware-dependent cases are explicitly tracked for later board testing.
