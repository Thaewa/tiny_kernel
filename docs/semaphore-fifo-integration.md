# Binary semaphore FIFO integration

## Changes and ownership

Each `osBinSem` now contains `value` and a `waiters` queue. Each TCB contains
one `waitNode` whose owner points back to that TCB. TCB layout is declared in
the internal `osKernelPrivate.h`; it is not an application API. The stack
pointer remains at offset zero, checked by a static assertion, preserving
PendSV's assembly contract. `osKernel.c` initializes the node when it creates
a thread. Public Take/Give signatures are unchanged.

`osBinSemInit` initializes both the value and the empty queue. Initialize
fresh semaphore storage before use. Reinitializing with outstanding waiters
is forbidden: it would discard their registration. Existing binaries must
be rebuilt because the semaphore and TCB layouts have changed.

## Block and handoff transitions

Take still consumes an available signal immediately. When unavailable:

1. Validate the original caller context.
2. Within the critical section, validate the current task and append its
   node to this semaphore's queue.
3. Only after successful insertion, set waitSem and state BLOCKED.
4. Publish state, request PendSV, then restore the saved interrupt mask.

Give holds the critical section while it validates the queue and selects
its head. It validates that the head's owner is an initialized TCB, that
the node actually belongs to that TCB, and that the task is BLOCKED on this
semaphore. Only then does it pop, clear waitSem and mark the task READY.
If no waiter exists, Give stores value 1. Direct handoff leaves value zero:
the signal belongs to the awakened task, not to a competing future Take.
The readiness update and PendSV request are completed before releasing the
critical section. Queue failure is fatal rather than treated as empty.

These portable task-state transitions are in `Src/osKernelWait.c` so host
tests compile the real implementation independently of Cortex-M startup,
timers and assembly. A fixed-address-range check validates TCB ownership;
no TCB scan is used to select the waiter. Queue validation remains bounded
by eight nodes; FIFO selection does not introduce task priorities.

FIFO describes wake selection, not the execution order of several tasks
made ready simultaneously. The ready scheduler still uses round-robin.
There are no semaphore timeouts, task deletion or ISR-specific API contracts
in this milestone.

## Host verification

The new `Semaphore FIFO Integration` CI job builds production semaphore,
queue and kernel wait code with sanitizer instrumentation. Only port calls
and context dispatch are faked. The fixture supplies the real TCB layout.
It checks C,A,B waiting order despite A,B,C task-storage order, per-semaphore
isolation, rewait at the tail, direct-handoff reservation, duplicate blocking
and enqueue, and owner/wait relationship mismatch before dequeue.

The fake context request does not execute a stack switch. The harness must
inspect published BLOCKED/READY states and manually select a caller; it does
not claim that a blocking Take returned normally on a real CPU. Existing
semaphore-unit and generic queue jobs remain as separate regressions.

## Hardware diagnostic (mode 3)

In `Inc/osHardwareTest.h`, temporarily select:

```c
#define OS_HARDWARE_TEST_MODE OS_HW_TEST_SEMAPHORE_FIFO
```

Alternatively set the compiler symbol `OS_HARDWARE_TEST_MODE=3`, but do not
select the mode in both places. Refresh the project so CubeIDE discovers
`osKernelWait.c`, then clean, build, flash and run without breakpoints.
Open UART at 115200 baud. The startup banner identifies this mode.

The test creates A,B,C in that order but staggers their first Take so C,A,B
enter the queue. IDs are A=0, B=1, C=2. The producer waits for all three
nodes, Gives one signal, waits for the selected task to acknowledge, and
logs the ID. Each waiter immediately Takes again to exercise node reuse.
Only the producer prints, avoiding simultaneous task printf calls.

Expected output over at least 30 Gives (ten complete rotations):

```text
FIFO PASS give=1 wake=2 expected=2 failures=0 ...
FIFO PASS give=2 wake=0 expected=0 failures=0 ...
FIFO PASS give=3 wake=1 expected=1 failures=0 ...
FIFO PASS give=4 wake=2 expected=2 failures=0 ...
```

Every record must have PASS, matching wake/expected and failures zero.
Missing initial/requeued waiters produce a timeout failure rather than a
silent wait. A stopped UART or osPortBlockingFault is also a failed run.
The LED toggles per Give but cannot prove ordering by itself.

Return the selector to OS_HW_TEST_SEMAPHORE, rebuild and flash after testing.
Verify the original producer/consumer demo again before committing. Record
host and board outcomes separately; host success does not verify PendSV.
