#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "osKernelPrivate.h"
#include "osKernelInternal.h"
#include "osBlocking.h"
#include "fake_os_port.h"

/* Compile actual semaphore, FIFO and kernel wait code. Only physical
 * exception dispatch and register access are faked. A blocked Take returns
 * to this harness, which inspects state and manually selects another task. */
tcbType tcbs[OS_MAX_THREADS];
tcbType *currentPt;
volatile uint8_t thread_count;
volatile uint32_t osKernelRunning;
static unsigned int switchRequests;
static unsigned int checks;

#define CHECK(condition) do { \
	checks++; \
	if (!(condition)) { \
		fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #condition); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

void osKernelRequestContextSwitch(void) { switchRequests++; }

static void resetKernel(void)
{
	fakeOsPortReset();
	memset(tcbs, 0, sizeof(tcbs));
	thread_count = 4U;
	osKernelRunning = 1U;
	switchRequests = 0U;
	for (unsigned int i = 0U; i < thread_count; i++)
	{
		tcbs[i].state = THREAD_READY;
		CHECK(osWaitNodeInit(&tcbs[i].waitNode, &tcbs[i]) == OS_WAIT_OK);
	}
	currentPt = &tcbs[3];
}

static void checkDetached(unsigned int id)
{
	CHECK(tcbs[id].state == THREAD_READY);
	CHECK(tcbs[id].waitSem == NULL);
	CHECK(tcbs[id].waitNode.container == NULL);
	CHECK(tcbs[id].waitNode.next == NULL);
	CHECK(tcbs[id].waitNode.previous == NULL);
	CHECK(tcbs[id].waitNode.owner == &tcbs[id]);
}

static void blockAs(unsigned int id, osCountSem *sem)
{
	currentPt = &tcbs[id];
	osCountSemTake(sem);
	CHECK(tcbs[id].state == THREAD_BLOCKED);
	CHECK(tcbs[id].waitSem == &sem->core);
	CHECK(tcbs[id].waitNode.container == &sem->core.waiters);
	CHECK(sem->core.value == 0);
	CHECK(osWaitQueueValidate(&sem->core.waiters) == OS_WAIT_OK);
}

static void test_init_and_invalid_limits(void)
{
	osCountSem sem;
	resetKernel();
	CHECK(osCountSemInit(&sem, 3U, 2U));
	CHECK(sem.core.value == 2 && sem.maxCount == 3U);
	CHECK(sem.core.waiters.head == NULL && sem.core.waiters.tail == NULL);
	CHECK(sem.core.waiters.count == 0U);
	CHECK(!osCountSemInit(NULL, 3U, 0U));
	CHECK(!osCountSemInit(&sem, 0U, 0U));
	CHECK(!osCountSemInit(&sem, 3U, 4U));
	CHECK(!osCountSemInit(&sem, (uint32_t)INT32_MAX + 1U, 0U));
	CHECK(!osCountSemInit(&sem, UINT32_MAX, 0U));
	/* All rejected initializations leave the prior inactive storage intact. */
	CHECK(sem.core.value == 2 && sem.maxCount == 3U);
	CHECK(osWaitQueueValidate(&sem.core.waiters) == OS_WAIT_OK);
	puts("PASS: init limits and no partial mutation on invalid init");
}

static void test_accumulation_capacity_and_drain(void)
{
	osCountSem sem;
	resetKernel();
	CHECK(osCountSemInit(&sem, 3U, 0U));
	for (int32_t i = 1; i <= 3; i++)
	{
		CHECK(osCountSemGive(&sem));
		CHECK(sem.core.value == i);
	}
	CHECK(!osCountSemGive(&sem));
	CHECK(!osCountSemGive(&sem));
	CHECK(sem.core.value == 3 && switchRequests == 0U);
	CHECK(sem.core.waiters.count == 0U);
	CHECK(fakeOsPortEnterCriticalCallCount() == fakeOsPortExitCriticalCallCount());
	for (int32_t remaining = 2; remaining >= 0; remaining--)
	{
		osCountSemTake(&sem);
		CHECK(sem.core.value == remaining);
		checkDetached(3U);
		CHECK(switchRequests == 0U);
	}
	CHECK(osCountSemGive(&sem)); /* A slot was freed by Take. */
	CHECK(sem.core.value == 1);
	puts("PASS: accumulate, reject overflow, consume one token and reuse capacity");
}

static void test_signed_boundary(void)
{
	osCountSem sem;
	resetKernel();
	CHECK(osCountSemInit(&sem, (uint32_t)INT32_MAX, (uint32_t)INT32_MAX - 1U));
	CHECK(osCountSemGive(&sem));
	CHECK(sem.core.value == INT32_MAX);
	CHECK(!osCountSemGive(&sem));
	CHECK(sem.core.value == INT32_MAX);
	osCountSemTake(&sem);
	CHECK(sem.core.value == INT32_MAX - 1);
	CHECK(osCountSemGive(&sem));
	CHECK(sem.core.value == INT32_MAX);
	puts("PASS: INT32_MAX boundary without signed overflow");
}

static void test_fifo_and_direct_handoff(void)
{
	osCountSem sem;
	resetKernel();
	CHECK(osCountSemInit(&sem, 3U, 0U));
	blockAs(2U, &sem);
	blockAs(0U, &sem);
	blockAs(1U, &sem);
	currentPt = &tcbs[3];
	const unsigned int order[3] = {2U, 0U, 1U};
	for (unsigned int i = 0U; i < 3U; i++)
	{
		CHECK(osCountSemGive(&sem));
		checkDetached(order[i]);
		CHECK(sem.core.value == 0); /* Handed-off tokens are not stored. */
		CHECK(sem.core.waiters.count == 2U - i);
		for (unsigned int j = i + 1U; j < 3U; j++)
		{
			CHECK(tcbs[order[j]].state == THREAD_BLOCKED);
		}
	}
	CHECK(switchRequests == 6U);
	CHECK(osCountSemGive(&sem)); /* No waiter now: accumulate instead. */
	CHECK(sem.core.value == 1 && switchRequests == 6U);
	puts("PASS: oldest waiter receives exactly one token before accumulation");
}

static void test_handoff_reservation_and_mixed_queues(void)
{
	osCountSem count;
	osBinSem binary;
	resetKernel();
	CHECK(osCountSemInit(&count, 2U, 0U));
	osBinSemInit(&binary, 0);
	blockAs(2U, &count);
	currentPt = &tcbs[1];
	osBinSemTake(&binary);
	CHECK(tcbs[1].state == THREAD_BLOCKED);
	currentPt = &tcbs[3];
	CHECK(osCountSemGive(&count));
	checkDetached(2U);
	CHECK(tcbs[1].state == THREAD_BLOCKED);
	/* C owns the handed-off token: A cannot steal it while C is ready. */
	blockAs(0U, &count);
	currentPt = &tcbs[3];
	osBinSemGive(&binary);
	checkDetached(1U);
	CHECK(tcbs[0].state == THREAD_BLOCKED);
	CHECK(osCountSemGive(&count));
	checkDetached(0U);
	/* The awakened node can safely rewait at the tail. */
	blockAs(2U, &count);
	blockAs(0U, &count);
	CHECK(count.core.waiters.head == &tcbs[2].waitNode);
	CHECK(count.core.waiters.tail == &tcbs[0].waitNode);
	puts("PASS: reserved handoff, binary/counting isolation and node reuse");
}

static void test_guard(osExecutionContext caller, uint32_t expectedErrors,
                       uint32_t running, uint32_t initial)
{
	static osCountSem sem;
	resetKernel();
	CHECK(osCountSemInit(&sem, 3U, initial));
	osKernelRunning = running;
	fakeOsPortSetExecutionContext(caller);
	fakeOsPortFaultExpected = 1;
	if (setjmp(fakeOsPortFaultJump) == 0)
	{
		osCountSemTake(&sem);
		CHECK(0);
	}
	CHECK(osLastBlockingFault.errors == expectedErrors);
	CHECK(osLastBlockingFault.operation == OS_BLOCK_OPERATION_COUNT_SEMAPHORE);
	CHECK(osLastBlockingFault.caller.primask == caller.primask);
	CHECK(sem.core.value == (int32_t)initial);
	CHECK(sem.core.waiters.count == 0U);
	CHECK(switchRequests == 0U);
	checkDetached(3U);
}

static void test_blocking_contract(void)
{
	test_guard((osExecutionContext){0U, 1U, 0U, 0U}, OS_BLOCK_ERROR_PRIMASK, 1U, 0U);
	test_guard((osExecutionContext){0U, 0U, 0x50U, 0U}, OS_BLOCK_ERROR_BASEPRI, 1U, 0U);
	test_guard((osExecutionContext){0U, 0U, 0U, 1U}, OS_BLOCK_ERROR_FAULTMASK, 1U, 0U);
	test_guard((osExecutionContext){16U, 0U, 0U, 0U}, OS_BLOCK_ERROR_ISR, 1U, 2U);
	test_guard((osExecutionContext){0U, 0U, 0U, 0U}, OS_BLOCK_ERROR_NO_TASK, 0U, 2U);
	osCountSem sem;
	resetKernel();
	CHECK(osCountSemInit(&sem, 3U, 2U));
	fakeOsPortSetExecutionContext((osExecutionContext){0U, 1U, 0x50U, 1U});
	osCountSemTake(&sem);
	CHECK(sem.core.value == 1 && switchRequests == 0U);
	CHECK(fakeOsPortLastRestoredIrqState() == FAKE_OS_PORT_SAVED_IRQ_STATE);
	checkDetached(3U);
	puts("PASS: shared blocking guards and allowed masked fast take");
}

int main(void)
{
	test_init_and_invalid_limits();
	test_accumulation_capacity_and_drain();
	test_signed_boundary();
	test_fifo_and_direct_handoff();
	test_handoff_reservation_and_mixed_queues();
	test_blocking_contract();
	printf("All %u counting semaphore host checks passed.\n", checks);
	return EXIT_SUCCESS;
}
