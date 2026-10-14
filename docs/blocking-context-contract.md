# Blocking context contract

The semaphore take and delay APIs are task-only APIs. They require a running
kernel and a current thread in the ready state. `osBinSemInit` remains usable
before launch. This milestone does not define an ISR API for semaphore give.

Before publishing a blocked/sleeping state, the caller must have IPSR,
PRIMASK, BASEPRI and FAULTMASK all zero. Any nonzero BASEPRI is rejected
conservatively. The kernel captures these registers before it enters its
own PRIMASK critical section, then uses the saved caller context for checks.

A ready task may take an immediately available semaphore while masked.
An unavailable take must fail before changing the task's wait relationship.
Take from an ISR or without a running ready task fails even if a signal is
available. `osDelay(0)` retains the existing sleep/yield behavior and requires
the same valid blocking context as a positive delay.

On misuse, `osPortBlockingFault` records `osLastBlockingFault`, disables
interrupts and loops without returning. Inspect it in the debugger:

| errors bit | Meaning |
| --- | --- |
| 0 / 0x01 | ISR caller (IPSR nonzero) |
| 1 / 0x02 | Caller PRIMASK nonzero |
| 2 / 0x04 | Caller BASEPRI nonzero |
| 3 / 0x08 | Caller FAULTMASK nonzero |
| 4 / 0x10 | Kernel not running or current task not ready |
| 5 / 0x20 | Kernel waiter/queue invariant failure |
| 6 / 0x40 | Non-recursive mutex Take by its existing owner |

`operation` is 1 for semaphore take, 2 for delay, 3 for wait enqueue and 4
for wait dequeue, 5 for counting semaphore Take, 6 for mutex Take and 7
for mutex Give. Queue faults in kernel wait transitions also record
`osLastWaitQueueResult` using
the `osWaitResult` enum. For queue faults, registers are captured inside
the protected operation, so PRIMASK may represent the internal lock.
For caller-context faults, `caller` holds the
original registers, not the interrupt mask subsequently set by the kernel.
The guard remains active in release builds. It does not open a caller's
critical section, print through UART, or return as if a void API succeeded.
Reset the target after inspecting a fault.

Blocking state publication and PendSV request occur within one critical
section. Restoring the saved mask permits the pending switch to run. This
prevents an interrupt from observing a blocked task before its switch has
been requested. Semaphore FIFO integration additionally validates node
membership and waiter identity before publishing a state transition.

## Verification

The host semaphore suite uses the real inline policy and real semaphore
implementation. Fake register snapshots test each forbidden mask, ISR and
missing-task cases, combined reasons, preserved caller diagnostics, no wait
relationship mutation, no scheduling request, and the allowed masked fast
path. A fatal fake jumps to the test harness; it never returns to the API.
Existing valid take/give tests remain in the suite.

Host fakes do not verify Cortex-M register reads, the actual delay function,
or physical PendSV dispatch. On hardware, run the normal semaphore demo to
confirm valid take/delay still work. For a separate debugger experiment,
take an unavailable semaphore after `__disable_irq()` from a task, or call
`osDelay(1)` in that context. Expect error 0x02 with operation 1 or 2 and
caller PRIMASK 1, before the task state changes. Do not commit that misuse
into the default application; reset and restore the demo afterward.
