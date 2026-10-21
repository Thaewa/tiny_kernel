#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "osThread.h"
#include "osKernelPrivate.h"
#include "fake_os_port.h"

/* Only dispatch/IRQ are faked. Production code builds frames, checks ranges,
 * publishes the circular list and queries actual registered storage. */
tcbType tcbs[OS_MAX_THREADS];
tcbType *currentPt;
volatile uint8_t thread_count;
volatile uint32_t osKernelRunning;
static unsigned int checks;
void osThreadExit(void) { abort(); } /* Frame target only; no host dispatch. */
static void task(void) { }
static uint32_t arena[2048] __attribute__((aligned(8)));

#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void reset(void)
{
    memset(tcbs, 0, sizeof(tcbs));
    for (uint32_t i = 0U; i < 2048U; i++) arena[i] = 0x12345678U;
    currentPt = NULL;
    thread_count = 0U;
    osKernelRunning = 0U;
    fakeOsPortReset();
}

static void checkMaskRestored(void)
{
    CHECK(fakeOsPortEnterCriticalCallCount() == fakeOsPortExitCriticalCallCount());
    CHECK(fakeOsPortLastRestoredIrqState() == FAKE_OS_PORT_SAVED_IRQ_STATE);
}

static void test_frame_and_mixed_sizes(void)
{
    reset();
    CHECK(osCreateThread(task) == 1U); /* Idle-like compatibility slot. */
    CHECK(currentPt == &tcbs[0] && tcbs[0].nextPt == &tcbs[0]);
    CHECK(osCreateThreadStatic(task, arena, 256U) == 1U);
    CHECK(osCreateThreadStatic(task, arena + 256U, 512U) == 1U);
    CHECK(osCreateThread(task) == 1U); /* Legacy works after static slots too. */
    CHECK(thread_count == 4U && currentPt == &tcbs[0]);
    CHECK(tcbs[0].nextPt == &tcbs[1] && tcbs[1].nextPt == &tcbs[2]);
    CHECK(tcbs[2].nextPt == &tcbs[3] && tcbs[3].nextPt == &tcbs[0]);
    for (uint8_t i = 0U; i < 4U; i++)
    {
        const uint32_t sizes[] = {400U, 256U, 512U, 400U};
        osStackUsage usage;
        CHECK(osGetThreadStackUsage(i, &usage));
        CHECK(usage.allocatedWords == sizes[i] && usage.guardWords == 8U);
        CHECK(usage.maximumUsedWords == 16U && usage.minimumFreeWords == sizes[i] - 24U);
        CHECK(usage.guardIntact);
        CHECK(tcbs[i].state == THREAD_READY && tcbs[i].waitSem == NULL && tcbs[i].waitMutex == NULL);
        CHECK(tcbs[i].waitNode.owner == &tcbs[i] && tcbs[i].waitNode.container == NULL);
        uint32_t *frame = (uint32_t *)tcbs[i].stackPt;
        CHECK(((uintptr_t)frame & 7U) == 0U);
        for (uint32_t r = 0U; r < 13U; r++) CHECK(frame[r] == 0xDEADBEEFU);
        CHECK(frame[13] == (uint32_t)(uintptr_t)osThreadExit);
        CHECK(frame[14] == (uint32_t)(uintptr_t)task && frame[15] == (1U << 24));
    }
    CHECK((uint32_t *)tcbs[1].stackPt == arena + 256U - 16U);
    CHECK((uint32_t *)tcbs[2].stackPt == arena + 768U - 16U);
    CHECK(arena[768] == 0x12345678U); /* Adjacent storage untouched. */
    arena[0] = 0U;
    arena[256U + 20U] = 0U;
    osStackUsage usage;
    CHECK(osGetThreadStackUsage(1U, &usage) && !usage.guardIntact);
    CHECK(osGetThreadStackUsage(2U, &usage) && usage.guardIntact);
    CHECK(usage.maximumUsedWords == 492U && usage.minimumFreeWords == 12U);
    checkMaskRestored();
    puts("PASS: mixed legacy/static sizes, context layout, links and actual-buffer queries");
}

/* Every rejected registration must leave candidate bytes, published TCBs,
 * currentPt and slot count unchanged. Metadata is tested by subsequent use. */
static void rejected(void (*entry)(void), uint32_t *stack, uint32_t words)
{
    tcbType before[OS_MAX_THREADS];
    uint32_t bytes[2048];
    memcpy(before, tcbs, sizeof(before));
    memcpy(bytes, arena, sizeof(bytes));
    uint8_t count = thread_count;
    tcbType *current = currentPt;
    CHECK(osCreateThreadStatic(entry, stack, words) == 0U);
    CHECK(thread_count == count && currentPt == current);
    CHECK(memcmp(before, tcbs, sizeof(before)) == 0);
    CHECK(memcmp(bytes, arena, sizeof(bytes)) == 0);
}

static void test_rejections(void)
{
    reset();
    rejected(NULL, arena, 256U);
    CHECK(osCreateThread(NULL) == 0U);
    rejected(task, NULL, 256U);
    rejected(task, arena, 0U);
    rejected(task, arena, OS_THREAD_MIN_STACK_WORDS - 2U);
    rejected(task, arena, 257U); /* Even top alignment is mandatory. */
    rejected(task, arena + 1U, 256U); /* Base only 4-byte aligned. */
    rejected(task, (uint32_t *)(UINTPTR_MAX & ~(uintptr_t)7U), 64U);
    CHECK(osCreateThreadStatic(task, arena + 256U, 256U) == 1U);
    rejected(task, arena + 256U, 256U); /* Exact reuse. */
    rejected(task, arena + 256U, 64U);  /* Contained. */
    rejected(task, arena + 128U, 512U); /* Encloses existing storage. */
    rejected(task, arena + 128U, 256U); /* Overlap at left edge. */
    rejected(task, arena + 384U, 256U); /* Overlap at right edge. */
    CHECK(osCreateThreadStatic(task, arena, 256U) == 1U); /* Touching is OK. */
    CHECK(osCreateThreadStatic(task, arena + 512U, 256U) == 1U);
    checkMaskRestored();
    puts("PASS: malformed/overflowed/overlapping ranges rejected before mutation");
}

static void test_compatibility_pool_and_capacity(void)
{
    reset();
    CHECK(osCreateThread(task) == 1U);
    uint32_t *base = (uint32_t *)tcbs[0].stackPt - (400U - 16U);
    rejected(task, base, 400U);
    /* Build an address in the next still-unused compatibility slot without
     * performing C pointer arithmetic outside the first row subarray. */
    uint32_t *next = (uint32_t *)((uintptr_t)base + 400U * sizeof(uint32_t));
    rejected(task, next, 400U);
    for (uint8_t i = 1U; i < OS_MAX_THREADS; i++)
    {
        CHECK(osCreateThreadStatic(task, arena + (i - 1U) * 128U, 128U) == 1U);
    }
    CHECK(thread_count == OS_MAX_THREADS);
    rejected(task, arena + 1024U, 128U);
    CHECK(osCreateThread(task) == 0U);
    tcbType *cursor = currentPt;
    for (uint8_t i = 0U; i < OS_MAX_THREADS; i++)
    {
        CHECK(cursor == &tcbs[i]);
        cursor = cursor->nextPt;
    }
    CHECK(cursor == currentPt);
    checkMaskRestored();
    puts("PASS: entire legacy pool reserved and capacity preserves circular topology");
}

static void test_context_and_query_contract(void)
{
    reset();
    osStackUsage usage = {11U, 12U, 13U, 14U, true};
    CHECK(!osGetThreadStackUsage(0U, &usage));
    fakeOsPortSetExecutionContext((osExecutionContext){3U, 0U, 0U, 0U});
    rejected(task, arena, 256U);
    CHECK(osCreateThread(task) == 0U);
    CHECK(!osGetThreadStackUsage(0U, &usage));
    CHECK(fakeOsPortEnterCriticalCallCount() == 1U); /* Only startup query. */
    fakeOsPortSetExecutionContext((osExecutionContext){0U, 1U, 0U, 0U});
    CHECK(osCreateThreadStatic(task, arena, OS_THREAD_MIN_STACK_WORDS) == 1U);
    checkMaskRestored();
    CHECK(!osGetThreadStackUsage(1U, &usage));
    CHECK(!osGetThreadStackUsage(255U, &usage));
    CHECK(!osGetThreadStackUsage(0U, NULL));
    CHECK(usage.allocatedWords == 11U && usage.guardWords == 12U &&
          usage.minimumFreeWords == 13U && usage.maximumUsedWords == 14U && usage.guardIntact);
    osKernelRunning = 1U;
    rejected(task, arena + 256U, 256U);
    CHECK(osCreateThread(task) == 0U);
    CHECK(osGetThreadStackUsage(0U, &usage)); /* Runtime query is allowed. */
    CHECK(usage.allocatedWords == OS_THREAD_MIN_STACK_WORDS);
    osKernelRunning = 0U;
    thread_count = 255U; /* Corrupt/out-of-bounds count must not index arrays. */
    rejected(task, arena + 256U, 256U);
    CHECK(osCreateThread(task) == 0U);
    CHECK(!osGetThreadStackUsage(0U, &usage));
    checkMaskRestored();
    puts("PASS: startup-only creation, ISR rejection, saved masks and query errors");
}

static void test_legacy_only(void)
{
    reset();
    for (uint8_t i = 0U; i < OS_MAX_THREADS; i++)
    {
        CHECK(osCreateThread(task) == 1U);
        osStackUsage usage;
        CHECK(osGetThreadStackUsage(i, &usage));
        CHECK(usage.allocatedWords == 400U && usage.maximumUsedWords == 16U);
        CHECK(usage.guardIntact && usage.minimumFreeWords == 376U);
        if (i > 0U)
            CHECK((uintptr_t)tcbs[i].stackPt - (uintptr_t)tcbs[i - 1U].stackPt == 1600U);
    }
    CHECK(osCreateThread(task) == 0U);
    CHECK(tcbs[OS_MAX_THREADS - 1U].nextPt == &tcbs[0]);
    checkMaskRestored();
    puts("PASS: all eight legacy slots keep distinct 400-word stacks");
}

int main(void)
{
    test_frame_and_mixed_sizes();
    test_rejections();
    test_compatibility_pool_and_capacity();
    test_context_and_query_contract();
    test_legacy_only();
    printf("All %u thread creation host checks passed.\n", checks);
    return EXIT_SUCCESS;
}
