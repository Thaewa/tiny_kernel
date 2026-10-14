# Basic non-recursive mutex

## API and ownership

```c
void osMutexInit(osMutex *mutex);
void osMutexTake(osMutex *mutex);
bool osMutexGive(osMutex *mutex);
```

Initialize fresh/inactive, live storage before use. Never reinitialize a
held mutex or one with waiters. Take/Give are task-only and require a running
kernel with a current ready task. All pointers must be valid and non-NULL.

A free mutex records the calling TCB as owner. Another task's Take appends
to the mutex's FIFO queue and blocks. Give succeeds only for the owner;
unlocked or wrong-owner Give returns false without changing queue, owner
or task states. Callers must check that result.

When there is a waiter, Give validates its node, TCB identity and wait
relationship, pops the oldest node, transfers owner, clears waitMutex and
then publishes READY. Ownership never becomes NULL during this handoff.
The awakened Take resumes as the owner without taking a second time. An
immediate retake by the releasing task must join behind existing waiters.
When nobody waits, Give clears owner and does not request a switch.

`owner` is an opaque kernel identity; applications must not set it. This is
different from a semaphore signal, which does not track who should Give.
`waitMutex` and `waitSem` are separate TCB relationships, but share a single
waitNode. Kernel transitions reject conflicting relationships before insert.

## Context guards and limits

Take by the current owner is fatal: this is a non-recursive mutex, so there
is no recursion counter. The fault records errors 0x40 and operation 6,
before any queue insertion. Contended Take requires caller PRIMASK, BASEPRI
and FAULTMASK all zero. Immediate free acquisition may preserve caller
masks, as it does not block. Give never blocks and may preserve caller masks;
any requested handoff remains pending until those masks permit PendSV.

ISR or pre-launch calls are fatal even if the mutex is free. Invalid caller
context uses the existing error bits with operation 6 (Take) or 7 (Give).
Queue failures use errors 0x20 and operations 3/4 where detected in kernel
wait transitions. `osLastWaitQueueResult` applies to those kernel queue
faults; free-mutex topology failure detected in Take uses operation 6.

There is no task priority, priority inheritance, recursive acquisition,
timeout, owner-death recovery, task deletion handling, or general deadlock
detection. Two tasks can still deadlock by acquiring different mutexes in
opposite order. Applications must keep a consistent lock order and release
all held mutexes. Priority inheritance will need the future priority
scheduler and rules for multiple held mutexes and chains of waiting tasks.

## Host verification

`test_mutex.c` uses production mutex, queue and kernel wait code, replacing
only port/dispatch with fakes. Coverage includes owner-only release,
unlocked/double Give, FIFO selection, ownership reservation before dispatch,
previous-owner rewait, self-Take rejection, each scheduling mask, task-only
use, masked fast paths, semaphore/mutex wait isolation, invalid waiter
relationships before pop and 1000 transfers with reused nodes.

The `Basic Mutex` Actions job uses strict warnings, optimization, ASan,
UBSan and a runtime timeout. The harness manually selects callers after fake
dispatch and does not prove Cortex-M stack switching. Existing semaphore,
counting and generic queue suites remain regressions.

## Hardware mode 5

Temporarily select `OS_HW_TEST_MUTEX` in `Inc/osHardwareTest.h`, or define
`OS_HARDWARE_TEST_MODE=5` in compiler symbols, not both. Refresh, clean,
build, flash and run with UART at 115200 and no breakpoints.

Two workers acquire, remember the shared count, deliberately delay 10 ms
WHILE owning the mutex, update the shared count and release. This forces
contention and tests that delay/preemption does not bypass ownership.
Sleeping while holding a mutex is used here only as a diagnostic; real
critical work should normally be short. An active-worker marker detects
overlap, while shared==A+B detects lost updates.

The reporter attempts a foreign Give while a worker is observed holding
the mutex. Observation and Give are protected by one short critical section
to avoid a release between them. Give must return false. The reporter then
acquires legitimately, snapshots shared data, releases, and prints outside
the mutex. Only this task prints.

Observe at least 20 records:

```text
MUTEX PASS shared=... A=... B=... rejected=... failures=0 time=... ms
```

Both worker counts and rejected must advance, shared must equal A+B, and
failures must stay zero. A stopped UART or fatal handler is also a failed
observation; no reporter can print after losing all scheduling progress.
The mode proves contention/protected updates and wrong-owner rejection,
not priority inheritance or exhaustive CPU context preservation.

Return the selector to `OS_HW_TEST_SEMAPHORE` and rebuild/flash the normal
demo before committing. A separate optional debugger experiment can Take
the same mutex twice in one task and inspect error 0x40, operation 6; remove
that deliberate misuse afterward and reset to resume normal execution.
