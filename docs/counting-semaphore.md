# Counting semaphore contract and verification

## Public API

```c
bool osCountSemInit(osCountSem *sem, uint32_t maxCount, uint32_t initialCount);
void osCountSemTake(osCountSem *sem);
bool osCountSemGive(osCountSem *sem);
```

Init accepts maxCount from 1 through INT32_MAX and initialCount from zero
through maxCount. NULL or invalid limits return false before writing fields.
Count storage remains signed to preserve the existing binary representation;
this version does not support UINT32_MAX capacity. Check Init's result before
using the object. Valid Take/Give require live, initialized non-NULL storage.
Never reinitialize a semaphore while tasks are waiting or it is in use.

Take consumes exactly one stored token. When zero, the caller is placed in
FIFO order and blocks until a Give hands it one token. Give wakes exactly
one oldest waiter first, leaving stored count zero. With no waiter it adds
one token, or returns false if capacity is already full. Overflow does not
wrap, silently grow the count, or block the giving task. Applications must
decide what a rejected Give means for their resource/event accounting.

For example, maxCount=3, initialCount=0 and five Gives without a consumer
produce true,true,true,false,false and count 3. Three Takes produce counts
2,1,0; the fourth Take blocks. Binary Give still coalesces repeated signals
at one through its existing void API. A capacity-one counting semaphore
reports overflow, unlike binary signal coalescing.

## Shared implementation

`osSemaphoreCore` holds the count and FIFO queue. `osBinSem` is an alias of
that core, preserving existing binary field access and layout. `osCountSem`
contains a core and its immutable maxCount. Its core is an implementation
detail; do not pass it to binary APIs or modify count/queue fields manually.

Shared take/give helpers in `Src/osSemaphore.c` avoid duplicating interrupt
locking, barriers, FIFO handoff and context-switch requests. The kernel's
waitSem identifies the core, so binary and counting objects share the same
one-node-per-TCB wait machinery while keeping separate queues. No heap,
stack API change, task priority or mutex ownership is introduced here.

Take uses the existing blocking-context guard: ready task, running kernel,
not ISR, and scheduling unmasked when blocking is necessary. A ready task
may consume an immediately available token with caller masks set. Invalid
counting Take records operation 5 in osLastBlockingFault. Queue failures
continue using operations 3/4. This milestone supplies task use; no ISR API
contract or timeout is defined.

## Host coverage

`tests/host/test_counting_semaphore.c` compiles production semaphore, queue
and kernel-wait code, faking only port instructions and context dispatch.
It checks initialization errors without partial mutation, accumulation,
full-count rejection, token-by-token drain, INT32_MAX arithmetic, FIFO wake
selection, direct-handoff reservation, binary/counting queue isolation,
node reuse, invalid blocking contexts and allowed masked fast Take.

The `Counting Semaphore` Actions job uses strict warnings, optimized code,
AddressSanitizer, UndefinedBehaviorSanitizer and a bounded runtime. Existing
binary, generic queue and FIFO integration jobs remain regressions. Host
tests inspect task states after fake dispatch and do not execute PendSV.

## Hardware mode 4

Temporarily select OS_HW_TEST_COUNTING_SEMAPHORE in Inc/osHardwareTest.h,
or set OS_HARDWARE_TEST_MODE=4 in compiler symbols, but not both. Refresh,
clean, build, flash and run with UART at 115200 without breakpoints.

The producer Gives five times while the consumer is initially sleeping.
With capacity three, the first line must report:

```text
COUNT burst PASS accepted=3 rejected=2 consumed=0 stored=3 failures=0
```

The consumer then drains three tokens and blocks. The producer periodically
Gives to that waiter; the consumer acknowledges and immediately rewaits.
Observe at least 20 handoff records with accepted==consumed, rejected=2,
stored=0, waiters=1 and failures=0:

```text
COUNT handoff PASS accepted=4 consumed=4 rejected=2 stored=0 waiters=1 ...
COUNT handoff PASS accepted=5 consumed=5 rejected=2 stored=0 waiters=1 ...
```

Only the producer prints. The LED toggles on handoffs but cannot prove token
accounting. A drain/rewait timeout reports FAIL. UART stopping or entering
the fatal handler is also a failed observation. Capture the initial burst
line as well as repeated handoffs; steady handoffs alone do not demonstrate
accumulation or overflow handling.

Return the default selector to OS_HW_TEST_SEMAPHORE and rebuild/flash after
testing. Record host and MCU outcomes separately before merging the feature.
