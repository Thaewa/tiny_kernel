#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "osKernelPrivate.h"
#include "osKernelInternal.h"
#include "osBlocking.h"
#include "fake_os_port.h"

/* Actual kernel wait code uses this storage just as it does on the target.
 * The host does not execute PendSV: Take returns to the harness after its
 * request, so tests inspect the published state instead of pretending that
 * a blocked task actually resumed. Cortex-M execution remains a board test. */
tcbType tcbs[OS_MAX_THREADS];
tcbType *currentPt;
volatile uint8_t thread_count;
volatile uint32_t osKernelRunning;
static unsigned int checks;
static unsigned int switchRequests;

#define CHECK(condition) do { \
	checks++; \
	if (!(condition)) { \
		fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #condition); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

void osKernelRequestContextSwitch(void)
{
	switchRequests++;
}

static void resetKernel(void)
{
	fakeOsPortReset();
	memset(tcbs, 0, sizeof(tcbs));
	thread_count = 4U;
	osKernelRunning = 1U;
	switchRequests = 0U;
	osLastWaitQueueResult = 0U;
	for (unsigned int i = 0U; i < thread_count; i++)
	{
		tcbs[i].state = THREAD_READY;
		CHECK(osWaitNodeInit(&tcbs[i].waitNode, &tcbs[i]) == OS_WAIT_OK);
	}
	currentPt = &tcbs[3]; /* Producer/observer, not a waiter. */
}

static void takeAs(unsigned int id, osBinSem *sem)
{
	currentPt = &tcbs[id];
	osBinSemTake(sem);
	CHECK(tcbs[id].state == THREAD_BLOCKED);
	CHECK(tcbs[id].waitSem == sem);
	CHECK(tcbs[id].waitNode.container == &sem->waiters);
	CHECK(osWaitQueueValidate(&sem->waiters) == OS_WAIT_OK);
}

static void checkReady(unsigned int id)
{
	CHECK(tcbs[id].state == THREAD_READY);
	CHECK(tcbs[id].waitSem == NULL);
	CHECK(tcbs[id].waitNode.container == NULL);
	CHECK(tcbs[id].waitNode.next == NULL);
	CHECK(tcbs[id].waitNode.previous == NULL);
	CHECK(tcbs[id].waitNode.owner == &tcbs[id]);
}

static void test_fifo_not_creation_order(void)
{
	osBinSem sem;
	resetKernel();
	osBinSemInit(&sem, 0);
	/* Thread storage is A,B,C; deliberately enqueue C,A,B instead. */
	takeAs(2U, &sem);
	takeAs(0U, &sem);
	takeAs(1U, &sem);
	CHECK(sem.waiters.count == 3U);
	CHECK(sem.waiters.head == &tcbs[2].waitNode);
	CHECK(sem.waiters.tail == &tcbs[1].waitNode);
	currentPt = &tcbs[3];
	const unsigned int order[3] = {2U, 0U, 1U};
	for (unsigned int i = 0U; i < 3U; i++)
	{
		osBinSemGive(&sem);
		checkReady(order[i]);
		CHECK(sem.value == 0);
		CHECK(sem.waiters.count == 2U - i);
		for (unsigned int j = i + 1U; j < 3U; j++)
		{
			CHECK(tcbs[order[j]].state == THREAD_BLOCKED);
		}
		CHECK(osWaitQueueValidate(&sem.waiters) == OS_WAIT_OK);
	}
	CHECK(switchRequests == 6U); /* Three blocks and three handoffs. */
	osBinSemGive(&sem);
	osBinSemGive(&sem);
	CHECK(sem.value == 1 && switchRequests == 6U);
	osBinSemTake(&sem);
	CHECK(sem.value == 0 && switchRequests == 6U);
	checkReady(3U);
	puts("PASS: C,A,B FIFO selection and binary behavior after queue drains");
}

static void test_independent_semaphores_and_rewait(void)
{
	osBinSem first, second;
	resetKernel();
	osBinSemInit(&first, 0);
	osBinSemInit(&second, 0);
	takeAs(0U, &first);
	takeAs(1U, &second);
	takeAs(2U, &first);
	currentPt = &tcbs[3];
	osBinSemGive(&second);
	checkReady(1U);
	CHECK(first.waiters.count == 2U && second.waiters.count == 0U);
	CHECK(tcbs[0].state == THREAD_BLOCKED && tcbs[2].state == THREAD_BLOCKED);
	osBinSemGive(&first);
	checkReady(0U);
	/* Reusing A must append behind C, not regain its old position. */
	takeAs(0U, &first);
	CHECK(first.waiters.head == &tcbs[2].waitNode);
	CHECK(first.waiters.tail == &tcbs[0].waitNode);
	currentPt = &tcbs[3];
	osBinSemGive(&first);
	checkReady(2U);
	CHECK(tcbs[0].state == THREAD_BLOCKED);
	osBinSemGive(&first);
	checkReady(0U);
	puts("PASS: per-semaphore isolation and rewaiting at the tail");
}

static void test_handoff_cannot_be_stolen(void)
{
	osBinSem sem;
	resetKernel();
	osBinSemInit(&sem, 0);
	takeAs(2U, &sem);
	currentPt = &tcbs[3];
	osBinSemGive(&sem);
	checkReady(2U);
	CHECK(sem.value == 0);
	/* The signal belongs to C even before C runs. A competing Take must
	 * wait for another Give rather than consume C's reserved signal. */
	takeAs(0U, &sem);
	CHECK(sem.waiters.count == 1U && tcbs[2].state == THREAD_READY);
	currentPt = &tcbs[3];
	osBinSemGive(&sem);
	checkReady(0U);
	CHECK(sem.value == 0 && sem.waiters.count == 0U);
	puts("PASS: direct handoff cannot be consumed by a competing task");
}

static void test_duplicate_does_not_damage_queue(void)
{
	static osBinSem sem;
	resetKernel();
	osBinSemInit(&sem, 0);
	takeAs(0U, &sem);
	fakeOsPortFaultExpected = 1;
	if (setjmp(fakeOsPortFaultJump) == 0)
	{
		/* A blocked task must not reach Take again before it resumes. */
		osBinSemTake(&sem);
		CHECK(0);
	}
	CHECK(osLastBlockingFault.errors == OS_BLOCK_ERROR_NO_TASK);
	CHECK(osLastBlockingFault.operation == OS_BLOCK_OPERATION_SEMAPHORE);
	CHECK(sem.waiters.count == 1U && sem.waiters.head == &tcbs[0].waitNode);
	CHECK(tcbs[0].state == THREAD_BLOCKED);
	CHECK(osWaitQueueValidate(&sem.waiters) == OS_WAIT_OK);
	/* Also simulate an inconsistent READY state with a node still linked.
	 * Internal enqueue must reject without linking the same node twice. */
	tcbs[0].state = THREAD_READY;
	tcbs[0].waitSem = NULL;
	if (setjmp(fakeOsPortFaultJump) == 0)
	{
		osKernelBlockCurrentOnSemaphore(&sem);
		CHECK(0);
	}
	CHECK(osLastBlockingFault.errors == OS_BLOCK_ERROR_WAIT_QUEUE);
	CHECK(osLastWaitQueueResult == OS_WAIT_ALREADY_LINKED);
	CHECK(tcbs[0].state == THREAD_READY && tcbs[0].waitSem == NULL);
	CHECK(sem.waiters.count == 1U && tcbs[0].waitNode.next == NULL);
	CHECK(osWaitQueueValidate(&sem.waiters) == OS_WAIT_OK);
	puts("PASS: duplicate blocking and duplicate enqueue fail before link mutation");
}

static void test_bad_owner_or_state_fails_before_pop(void)
{
	static osBinSem sem;
	resetKernel();
	osBinSemInit(&sem, 0);
	takeAs(0U, &sem);
	currentPt = &tcbs[3];
	tcbs[0].waitNode.owner = &tcbs[1];
	fakeOsPortFaultExpected = 1;
	if (setjmp(fakeOsPortFaultJump) == 0)
	{
		osBinSemGive(&sem);
		CHECK(0);
	}
	CHECK(osLastBlockingFault.operation == OS_BLOCK_OPERATION_WAIT_DEQUEUE);
	CHECK(osLastWaitQueueResult == OS_WAIT_CORRUPT);
	CHECK(sem.waiters.count == 1U && tcbs[0].state == THREAD_BLOCKED);
	CHECK(tcbs[0].waitNode.container == &sem.waiters);
	tcbs[0].waitNode.owner = &tcbs[0];
	tcbs[0].waitSem = NULL;
	if (setjmp(fakeOsPortFaultJump) == 0)
	{
		osBinSemGive(&sem);
		CHECK(0);
	}
	CHECK(sem.waiters.count == 1U && tcbs[0].state == THREAD_BLOCKED);
	CHECK(sem.value == 0);
	puts("PASS: waiter owner/wait relationship mismatch fails before dequeue");
}

int main(void)
{
	test_fifo_not_creation_order();
	test_independent_semaphores_and_rewait();
	test_handoff_cannot_be_stolen();
	test_duplicate_does_not_damage_queue();
	test_bad_owner_or_state_fails_before_pop();
	printf("All %u semaphore FIFO integration checks passed.\n", checks);
	return EXIT_SUCCESS;
}
