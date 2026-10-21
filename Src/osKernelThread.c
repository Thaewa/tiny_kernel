/* Thread admission and initial integer context, shared by both creation APIs.
 * Kept free of MCU headers so host tests exercise the real publication path.
 * Only the dispatch/IRQ implementation remains architecture-specific. */
#include "osThread.h"
#include "osKernelPrivate.h"
#include "osPort.h"

#define INITIAL_CONTEXT_WORDS 16U

_Static_assert(OS_THREAD_MIN_STACK_WORDS >=
               OS_THREAD_STACK_GUARD_WORDS + INITIAL_CONTEXT_WORDS,
               "Minimum stack must fit guard and integer context");
_Static_assert(OS_THREAD_DEFAULT_STACK_WORDS % 2U == 0U,
               "Every default stack must preserve 8-byte top alignment");
_Static_assert(OS_THREAD_DEFAULT_STACK_WORDS >= OS_THREAD_MIN_STACK_WORDS,
               "Compatibility stacks must meet the admission minimum");

/* Compatibility storage remains reserved even for externally backed slots.
 * This milestone adds sizing/control, not a default-pool RAM optimization. */
static uint32_t defaultStacks[OS_MAX_THREADS][OS_THREAD_DEFAULT_STACK_WORDS]
    __attribute__((aligned(8)));
static uint32_t *threadStacks[OS_MAX_THREADS];
static uint32_t threadStackWords[OS_MAX_THREADS];

/* Flat-address Cortex-M RAM ranges are compared as integers, not by ordering
 * unrelated C pointers. Validate multiplication/addition before constructing
 * the exclusive end. This checks shape, not whether an address is real RAM. */
static bool stackRange(uint32_t *stack, uint32_t words,
                       uintptr_t *start, uintptr_t *end)
{
    uintptr_t base = (uintptr_t)stack;
    if (stack == NULL || (base & 7U) != 0U ||
        words < OS_THREAD_MIN_STACK_WORDS || (words & 1U) != 0U ||
        words > (UINTPTR_MAX - base) / sizeof(*stack)) return false;
    *start = base;
    *end = base + (uintptr_t)words * sizeof(*stack);
    return true;
}

static bool overlaps(uintptr_t start, uintptr_t end,
                     uintptr_t otherStart, uintptr_t otherEnd)
{
    return start < otherEnd && otherStart < end;
}

/* Called with IRQs masked only AFTER every rejection check has passed.
 * The frame order is the existing PendSV/bootstrap ABI; stackPt stays the
 * first TCB member. There is still no floating-point context preservation. */
static void prepareThread(uint8_t slot, void (*task)(void),
                          uint32_t *stack, uint32_t words)
{
    (void)osStackInitialize(stack, words, OS_THREAD_STACK_GUARD_WORDS);
    uint32_t *frame = stack + words - INITIAL_CONTEXT_WORDS;
    for (uint32_t i = 0U; i < INITIAL_CONTEXT_WORDS; i++) frame[i] = 0xDEADBEEFU;
    frame[13] = (uint32_t)(uintptr_t)osThreadExit; /* Hardware LR. */
    frame[14] = (uint32_t)(uintptr_t)task;        /* Hardware PC. */
    frame[15] = 1U << 24;                       /* xPSR Thumb bit. */
    tcbs[slot].stackPt = (int32_t *)frame;
    tcbs[slot].state = THREAD_READY;
    tcbs[slot].waitSem = NULL;
    tcbs[slot].waitMutex = NULL;
    tcbs[slot].wake_at_ms = 0U;
    (void)osWaitNodeInit(&tcbs[slot].waitNode, &tcbs[slot]);
    threadStacks[slot] = stack;
    threadStackWords[slot] = words;
}

static uint8_t createThread(void (*task)(void), uint32_t *stack,
                             uint32_t words, bool useDefault)
{
    if (task == NULL || osPortGetExecutionContext().ipsr != 0U) return 0U;
    osIrqState saved = osPortEnterCritical();
    uint8_t created = 0U;
    if (osKernelRunning != 0U || thread_count >= OS_MAX_THREADS) goto done;
    uint8_t slot = thread_count;
    if (useDefault)
    {
        stack = defaultStacks[slot];
        words = OS_THREAD_DEFAULT_STACK_WORDS;
    }
    uintptr_t start, end;
    if (!stackRange(stack, words, &start, &end)) goto done;
    if (!useDefault)
    {
        /* Reserve the ENTIRE compatibility pool, including unused slots:
         * otherwise a later legacy creation could overwrite a static task. */
        uintptr_t pool = (uintptr_t)defaultStacks;
        if (overlaps(start, end, pool, pool + sizeof(defaultStacks))) goto done;
    }
    for (uint8_t i = 0U; i < slot; i++)
    {
        uintptr_t otherStart, otherEnd;
        if (!stackRange(threadStacks[i], threadStackWords[i], &otherStart, &otherEnd) ||
            overlaps(start, end, otherStart, otherEnd)) goto done;
    }
    prepareThread(slot, task, stack, words);
    tcbs[slot].nextPt = &tcbs[0];
    if (slot == 0U) currentPt = &tcbs[0];
    else tcbs[slot - 1U].nextPt = &tcbs[slot];
    /* Publish only a fully initialized TCB/frame. No failure below here. */
    thread_count = slot + 1U;
    created = 1U;
done:
    osPortExitCritical(saved);
    return created;
}

uint8_t osCreateThread(void (*task)(void))
{
    return createThread(task, NULL, 0U, true);
}

uint8_t osCreateThreadStatic(void (*task)(void), uint32_t *stack, uint32_t words)
{
    return createThread(task, stack, words, false);
}

bool osGetThreadStackUsage(uint8_t slot, osStackUsage *result)
{
    if (result == NULL || osPortGetExecutionContext().ipsr != 0U) return false;
    osIrqState saved = osPortEnterCritical();
    bool measured = false;
    if (thread_count <= OS_MAX_THREADS && slot < thread_count)
    {
        measured = osStackMeasure(threadStacks[slot], threadStackWords[slot],
                                  OS_THREAD_STACK_GUARD_WORDS, result);
    }
    osPortExitCritical(saved);
    return measured;
}
