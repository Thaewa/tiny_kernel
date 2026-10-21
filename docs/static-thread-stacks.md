# Caller-provided thread stacks

## API and ownership

```c
#include "osKernel.h"

static uint32_t workerStack[256] __attribute__((aligned(8)));

/* In startup, after hardware initialization and before kernel launch: */
osInit(); /* Creates idle with its legacy 400-word stack. */
if (!osCreateThreadStatic(worker, workerStack, 256U)) {
    /* Do not launch a partially created application. Handle the error. */
}
```

`osCreateThreadStatic(task, stack, stackWords)` returns 1 on success, 0 on
rejection, just like `osCreateThread(task)`. Neither returns a task handle.
One word is uint32_t (4 bytes on this target), NOT a byte. 256 words means
1024 bytes total, including eight guard words and the initial 16-word
integer context. The initial diagnostic accounting is 256=8+232+16.

The base must be 8-byte aligned and the count even, so the initial top and
saved context meet the Cortex-M stack alignment requirement. The admission
minimum is 64 words, not a recommended size or a worst-case guarantee.
MSP also carries interrupts and kernel frames. Keep workload-specific
headroom and use diagnostics; printf may need substantially more space.

Use exclusively assigned writable RAM lasting the entire task lifetime,
normally a static/global array. Do not use a local array from main or a
helper, reuse the buffer, repaint it, or modify it after registration.
There is no heap allocation, deletion or reclamation. The kernel cannot
verify RAM accessibility, the true size of a C object, or its lifetime.
In particular, never pass kernel data or other application objects as stacks.

The old API remains available and reserves 400 words per slot. Its entire
8-slot compatibility pool remains allocated even if some tasks use external
buffers. Static stacks therefore ADD RAM in this milestone; they do not
yet reclaim unused compatibility buffers. Eliminating that pool is separate
work, not an implicit optimization here.

## Admission and publication

Both creation APIs are startup-only and reject NULL task entries, ISR
callers, full capacity, and creation after kernel launch. There are still
eight total slots INCLUDING idle. All creation critical sections restore
the caller's previous interrupt mask instead of unconditionally enabling
interrupts. Masked startup calls are allowed; this does not authorize
masked blocking once the scheduler runs.

The static API also rejects NULL storage, invalid shape, address-end
overflow, exact reuse, partial overlap and containment relative to already
registered stacks. Touching but non-overlapping ranges are allowed. It
reserves the entire default pool against external registration, including
unused slots, so a future legacy creation cannot overwrite an existing
static task.

Every rejection occurs before stack painting or changes to a TCB, circular
links, currentPt, slot count or registration metadata. Successful creation:

1. Paints the selected buffer and installs the unchanged initial context.
2. Initializes task state and its unqueued wait node.
3. Registers the actual buffer and size for diagnostic queries.
4. Links the fully initialized TCB and publishes the new slot count.

`Src/osKernelThread.c` owns this common path and stack metadata.
`Src/osKernel.c` retains scheduler, timers, bootstrap and PendSV assembly.
The TCB stack pointer stays at offset zero; the context format is unchanged.
`osGetThreadStackUsage` now reads each registered buffer's actual size.
Its painted-write and sampled-canary limitations remain unchanged.

## Verification

The `Static Thread Creation` host CI job exercises the production creation
and query functions, with only IRQ/context capture and the task-return
target replaced. It checks mixed sizes and legacy calls, frame words,
alignment, accounting, canary damage, circular links, adjacent ranges,
overlap rejection without mutation, reserved pool protection, capacity,
startup/ISR contracts and mask restoration. Sanitizers do not execute
Cortex-M dispatch or prove hardware stack sufficiency.

Refresh the project in CubeIDE so it discovers the new osKernelThread.c
source, then rebuild. Do not flash an old ELF whose link omitted that file.
For hardware verification choose OS_HW_TEST_STATIC_STACKS (mode 7), rebuild
and flash. UART at 115200 should start with:

```text
Static stacks: words=400,256,384,512 guard=8; duplicate rejected.
```

Mode 7 reuses the shallow/deep workloads from mode 6, but creates user
tasks with separate static arrays: shallow=256, deep=384, reporter=512
words; idle retains the legacy 400. It first tries to register the shallow
buffer twice and refuses to launch if that duplicate is accepted.
Observe at least ten report batches with these exact allocated sizes,
intact canaries, positive free space, both workers progressing and
failures=0. Context switching across different stack sizes must actually
run; successful compile/host tests alone do not verify that.

Mode 6 remains the original all-400-word diagnostic. Mode 0 remains the
default normal semaphore demo; restore it after hardware testing. Hardware
results for mode 7 are pending until the new firmware is run on the board.
