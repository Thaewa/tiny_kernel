# Stack usage and sampled canaries

## Scope and API

```c
bool osGetThreadStackUsage(uint8_t threadIndex, osStackUsage *result);
```

The kernel still allocates 400 32-bit words (1600 bytes) per task, including
idle. The first eight words at the low-address end carry a distinct canary
pattern; the remaining 392 words (1568 bytes) form the reported usable
region. This allocation is unchanged. Task-creation signatures, stack
switch assembly and the 16-word initial context format are unchanged.

Slots are diagnostic storage indexes: idle is zero and user tasks follow
creation order. `osCreateThread` still returns success/failure, NOT this
index. Slots are not portable task handles; dynamic deletion is unsupported.

The query returns false for NULL output, ISR calls or an uncreated/out-of-
range slot and leaves the output unchanged. It may run after task creation
before launch or from task context. A true return means a valid measurement,
not necessarily a healthy canary: inspect `guardIntact` separately.

## How measurement works

Before installing a thread's initial context, osStackInitialize fills its
buffer. The portable osStackMeasure then scans an untouched low-address
prefix without changing the stack:

```text
low address                                                high address
[ 8 canary words ][ untouched fill ... ][ deepest writes / context ... ]
                  < minimumFreeWords >< maximumUsedWords              >
```

All fields are words. Multiply by four for bytes on this target.
`allocatedWords = guardWords + minimumFreeWords + maximumUsedWords`.
The initial artificial context already contributes 16 used words before
the thread runs. Unwinding SP does not repaint old writes, so historical
usage normally remains visible. Do not repaint a live stack to reset it.

This port uses MSP for both threads and exceptions. The footprint includes
interrupt/context frames, kernel calls and diagnostic calls on that task's
stack, not just its application locals. Sampling the current task also
measures some of the query's own footprint. Results depend on compiler,
optimization, call paths and interrupt timing.

An untouched-looking word may actually have been written with the same fill
value. A large reserved array may not have been written at all. The estimate
can therefore understate the deepest SP excursion and is not a guaranteed
worst-case bound. Keep headroom rather than shrinking to the measured number.

The query disables maskable interrupts while scanning up to 400 words,
restoring the original mask afterward. It is a diagnostic operation, not a
constant-time scheduler operation. NMI/HardFault are outside this protection.
The portable helper assumes accessible initialized storage and serialized
writers; it cannot validate arbitrary addresses or a false buffer length.

## Canary limitations

Canary damage reports guardIntact=false but does not stop the CPU. The
scheduler does not check it on every switch. No MPU, automatic fault handler
or saved-SP bounds enforcement is added. Overflow can corrupt other memory
before a query, jump over a canary, or destroy the task's ability to report.
Use this to gather evidence; it is not memory protection or a production
stack-safety guarantee.

## Verification

The host `Stack Diagnostics` job checks paint boundaries, accounting,
untouched gaps, persistent writes after simulated unwind, full usage,
individual canary corruption, initial-frame usage, fill-pattern ambiguity
and argument handling with strict warnings, NDEBUG and sanitizers. It tests
the portable helper, not Cortex-M stack dispatch or the query adapter.

On hardware select `OS_HW_TEST_STACK_DIAGNOSTICS` in Inc/osHardwareTest.h
(or compiler symbol OS_HARDWARE_TEST_MODE=6, not both). Refresh, clean,
build, flash and run with UART at 115200 without breakpoints.

This mode creates idle=0, shallow worker=1, deep worker=2 and reporter=3.
The deep worker writes an explicit volatile 64-word local array, keeping
it live across a delay. Both workers must keep progressing. The reporter
queries all four slots and also verifies that slot 255 is rejected.

Observe at least ten batches of four task records and one progress record:

```text
STACK PASS task=0 allocated=400 guard=8 free=... used=... canary=OK failures=0
STACK PASS task=1 allocated=400 guard=8 free=... used=... canary=OK failures=0
STACK PASS task=2 allocated=400 guard=8 free=... used=... canary=OK failures=0
STACK PASS task=3 allocated=400 guard=8 free=... used=... canary=OK failures=0
STACK progress PASS shallow=... deep=... failures=0
```

Check positive remaining space, intact canaries, word accounting and zero
failures. Compare deep vs shallow footprints, but do not expect an exact
64-word difference because call frames and interrupts also contribute.
The reporter may have a large footprint due to printf. A stopped UART is
not proof of health. Return the selector to OS_HW_TEST_SEMAPHORE and verify
the normal demo after testing; do not deliberately overflow a live MCU
stack merely to demonstrate canary failure.
