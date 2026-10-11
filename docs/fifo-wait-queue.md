# FIFO wait queue: structure and contract

This milestone adds a portable queue and host tests. The scheduler and
semaphore do not use it yet: semaphore wake-up still scans TCB storage.
Connecting the queue to task state transitions is the next change.

## Storage and ownership

The queue is intrusive: the caller provides each node rather than the queue
allocating memory. Later, each TCB can contain one wait node. Queue pointers
refer to those nodes, whose `owner` points back to their TCB. No malloc or
free is performed. The queue does not inspect or modify the owner payload.

```text
queue.head -> A <-> B <-> C <- queue.tail
             |     |     |
container: queue queue queue
owner:     TCB-A TCB-B TCB-C
```

Head is the oldest waiter; tail is the newest. Push appends at tail and pop
removes head. Unlike a circular scheduler list, both outer links are NULL.
A singleton has head == tail, next == NULL and previous == NULL. There is no
sentinel and no legitimate self-link in this queue.

`container` answers "which queue currently contains this node?" while
`owner` answers "which object does this node represent?" They are different
relationships. An unlinked node has container/next/previous all NULL, but its
owner survives removal. A removed node can immediately be reused elsewhere.

Initialize queue and node storage before first use. Do not reinitialize a
nonempty queue or a linked node; initialization is not removal. Keep all
objects alive while linked. One node can belong to at most one queue. Future
kernel integration must also ensure a task cannot bypass this invariant by
using several different wait nodes at once.

## Duplicate insertion and failure behavior

Pushing A again while it is already waiting returns `OS_WAIT_ALREADY_LINKED`
before modifying pointers. This applies to the same queue and another queue.
It prevents the insertion pattern that could otherwise connect A to itself.

Remove requires both a matching container and actual presence in the queue.
A forged/stale container pointer alone does not authorize unlinking. Pop and
remove clear membership and both links. The owner payload is preserved.

| Result | Meaning |
| --- | --- |
| OK | Operation completed / validation passed |
| EMPTY | No oldest waiter to pop |
| ALREADY_LINKED | Push target belongs to a queue |
| NOT_MEMBER | Remove target does not belong to this queue |
| FULL | Eight nodes are already present |
| INVALID_ARGUMENT | Required argument is NULL |
| CORRUPT | Accessible links, membership, or count violate invariants |

Failed push/remove do not modify either queue or node. Failed/empty pop sets
the output pointer to NULL, leaving the queue unchanged. Functions report
errors instead of silently repairing corruption. The future kernel caller
will decide which failures mean ordinary absence and which require a fatal
diagnostic. This standalone module does not halt the target itself.

## Bounded validation and synchronization

Before any mutation, validation checks endpoints, count, every node's
container, previous/next consistency and self-links. The walk processes no
more than eight nodes, matching the current kernel's thread-storage limit.
Both the count and an independent fixed bound limit traversal. A corrupt
count cannot disable cycle detection. If the thread limit grows, review
`OS_WAIT_QUEUE_MAX_NODES` along with the kernel limit.

Pointer edits themselves cost O(1), but the always-on validation costs O(n),
with n <= 8. Remove also scans for actual membership. This prioritizes clear
diagnostics for this small learning kernel; it is not an unbounded or purely
constant-time queue implementation.

Validation detects logical corruption among accessible, initialized objects.
It cannot safely dereference arbitrary damaged addresses or freed storage.
It also does not prevent concurrent accesses by itself. When connected to
the kernel, the caller must hold a critical section across validation,
pointer changes, thread-state changes and the scheduling request. Merely
adding `volatile` would not make a multi-pointer operation atomic.

## Tests and build

`tests/host/test_wait_queue.c` compiles the actual production queue without
fake hardware or a scheduler. Coverage includes empty/singleton queues,
FIFO order, 1000 pop/reinsert iterations, duplicate and cross-queue insert,
arbitrary removal, wrong-queue removal, capacity, null arguments, stale
links, self-links, multi-node cycles, forged membership, endpoint and count
corruption. Rejected operations are checked for unchanged queue/node state.

GitHub Actions has a separate `FIFO Wait Queue` job. It compiles with strict
warnings, optimization, AddressSanitizer and UndefinedBehaviorSanitizer,
then runs with a 15-second timeout. Explicit test checks remain enabled with
NDEBUG. Sanitizers supplement the invariant checks; they do not prove all
possible kernel interleavings safe.

Build locally with a native host GCC:

```sh
gcc -std=c11 -Wall -Wextra -Werror -pedantic -O2 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -IInc Src/osWaitQueue.c tests/host/test_wait_queue.c -o test_wait_queue
./test_wait_queue
```

CubeIDE discovers `Src/osWaitQueue.c` as a normal source file when it refreshes
the project. The unused queue functions may be removed from the final ELF by
linker garbage collection until semaphore integration uses them. A firmware
build is a portability check; queue behavior is verified by the host job.
