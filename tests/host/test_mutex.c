#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "osKernelPrivate.h"
#include "osKernelInternal.h"
#include "osBlocking.h"
#include "fake_os_port.h"

/* Real mutex/queue/kernel-wait code. Only CPU port and dispatch are faked.
 * The host observes published states and selects the next caller manually;
 * it does not pretend to execute a Cortex-M stack switch. */
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
	currentPt = &tcbs[0];
}

static void ready(unsigned int id)
{
	CHECK(tcbs[id].state == THREAD_READY);
	CHECK(tcbs[id].waitMutex == NULL && tcbs[id].waitSem == NULL);
	CHECK(tcbs[id].waitNode.container == NULL);
	CHECK(tcbs[id].waitNode.next == NULL && tcbs[id].waitNode.previous == NULL);
}

static void waitAs(unsigned int id, osMutex *mutex)
{
	currentPt = &tcbs[id];
	osMutexTake(mutex);
	CHECK(tcbs[id].state == THREAD_BLOCKED);
	CHECK(tcbs[id].waitMutex == mutex && tcbs[id].waitSem == NULL);
	CHECK(tcbs[id].waitNode.container == &mutex->waiters);
	CHECK(osWaitQueueValidate(&mutex->waiters) == OS_WAIT_OK);
}

static void test_ownership_and_fifo_reservation(void)
{
	osMutex mutex;
	resetKernel();
	osMutexInit(&mutex);
	CHECK(mutex.owner == NULL && mutex.waiters.count == 0U);
	CHECK(!osMutexGive(&mutex)); /* Unlocked/double release is rejected. */
	osMutexTake(&mutex);
	CHECK(mutex.owner == &tcbs[0] && switchRequests == 0U);
	waitAs(2U, &mutex);
	waitAs(1U, &mutex);
	CHECK(mutex.waiters.head == &tcbs[2].waitNode);
	currentPt = &tcbs[3];
	CHECK(!osMutexGive(&mutex));
	CHECK(mutex.owner == &tcbs[0] && mutex.waiters.count == 2U);
	CHECK(switchRequests == 2U);
	currentPt = &tcbs[0];
	CHECK(osMutexGive(&mutex));
	CHECK(mutex.owner == &tcbs[2]); /* Ownership reserved before C runs. */
	ready(2U);
	CHECK(tcbs[1].state == THREAD_BLOCKED);
	/* The previous owner cannot steal it: its Take joins behind B. */
	waitAs(0U, &mutex);
	CHECK(mutex.owner == &tcbs[2] && mutex.waiters.count == 2U);
	CHECK(mutex.waiters.tail == &tcbs[0].waitNode);
	currentPt = &tcbs[2];
	CHECK(osMutexGive(&mutex));
	CHECK(mutex.owner == &tcbs[1]);
	ready(1U);
	currentPt = &tcbs[1];
	CHECK(osMutexGive(&mutex));
	CHECK(mutex.owner == &tcbs[0]);
	ready(0U);
	currentPt = &tcbs[0];
	CHECK(osMutexGive(&mutex));
	CHECK(mutex.owner == NULL && mutex.waiters.count == 0U);
	CHECK(!osMutexGive(&mutex));
	CHECK(switchRequests == 6U);
	CHECK(fakeOsPortEnterCriticalCallCount() == fakeOsPortExitCriticalCallCount());
	puts("PASS: owner-only release, FIFO reservation, rewait and double Give");
}

static void test_self_take_rejected_before_wait(void)
{
	static osMutex mutex;
	resetKernel();
	osMutexInit(&mutex);
	osMutexTake(&mutex);
	fakeOsPortFaultExpected = 1;
	if (setjmp(fakeOsPortFaultJump) == 0)
	{
		osMutexTake(&mutex);
		CHECK(0);
	}
	CHECK(osLastBlockingFault.errors == OS_BLOCK_ERROR_MUTEX_RECURSION);
	CHECK(osLastBlockingFault.operation == OS_BLOCK_OPERATION_MUTEX_TAKE);
	CHECK(mutex.owner == &tcbs[0] && mutex.waiters.count == 0U);
	CHECK(switchRequests == 0U);
	ready(0U);
	puts("PASS: non-recursive self Take fails before node insertion");
}

static void test_busy_mask(osExecutionContext caller, uint32_t errors)
{
	static osMutex mutex;
	resetKernel();
	osMutexInit(&mutex);
	osMutexTake(&mutex);
	currentPt = &tcbs[1];
	fakeOsPortSetExecutionContext(caller);
	fakeOsPortFaultExpected = 1;
	if (setjmp(fakeOsPortFaultJump) == 0)
	{
		osMutexTake(&mutex);
		CHECK(0);
	}
	CHECK(osLastBlockingFault.errors == errors);
	CHECK(osLastBlockingFault.operation == OS_BLOCK_OPERATION_MUTEX_TAKE);
	CHECK(osLastBlockingFault.caller.primask == caller.primask);
	CHECK(mutex.owner == &tcbs[0] && mutex.waiters.count == 0U);
	CHECK(switchRequests == 0U);
	ready(1U);
}

static void test_caller_contract(void)
{
	test_busy_mask((osExecutionContext){0U, 1U, 0U, 0U}, OS_BLOCK_ERROR_PRIMASK);
	test_busy_mask((osExecutionContext){0U, 0U, 0x50U, 0U}, OS_BLOCK_ERROR_BASEPRI);
	test_busy_mask((osExecutionContext){0U, 0U, 0U, 1U}, OS_BLOCK_ERROR_FAULTMASK);
	static osMutex mutex;
	resetKernel();
	osMutexInit(&mutex);
	fakeOsPortSetExecutionContext((osExecutionContext){16U, 0U, 0U, 0U});
	fakeOsPortFaultExpected = 1;
	if (setjmp(fakeOsPortFaultJump) == 0)
	{
		osMutexTake(&mutex); /* Even a free mutex is task-only. */
		CHECK(0);
	}
	CHECK(osLastBlockingFault.errors == OS_BLOCK_ERROR_ISR);
	CHECK(mutex.owner == NULL && mutex.waiters.count == 0U);
	if (setjmp(fakeOsPortFaultJump) == 0)
	{
		(void)osMutexGive(&mutex);
		CHECK(0);
	}
	CHECK(osLastBlockingFault.operation == OS_BLOCK_OPERATION_MUTEX_GIVE);
	CHECK(osLastBlockingFault.errors == OS_BLOCK_ERROR_ISR);
	fakeOsPortSetExecutionContext((osExecutionContext){0U, 0U, 0U, 0U});
	osKernelRunning = 0U;
	if (setjmp(fakeOsPortFaultJump) == 0)
	{
		osMutexTake(&mutex);
		CHECK(0);
	}
	CHECK(osLastBlockingFault.errors == OS_BLOCK_ERROR_NO_TASK);
	CHECK(mutex.owner == NULL);
	/* Immediate acquisition and non-blocking release preserve caller masks. */
	resetKernel();
	osMutexInit(&mutex);
	fakeOsPortSetExecutionContext((osExecutionContext){0U, 1U, 0x50U, 1U});
	osMutexTake(&mutex);
	CHECK(mutex.owner == &tcbs[0]);
	CHECK(osMutexGive(&mutex));
	CHECK(mutex.owner == NULL && switchRequests == 0U);
	CHECK(fakeOsPortLastRestoredIrqState() == FAKE_OS_PORT_SAVED_IRQ_STATE);
	puts("PASS: masked contention, task-only contract and masked fast paths");
}

static void test_mixed_waits_and_bad_handoff(void)
{
	static osMutex mutex;
	static osBinSem sem;
	resetKernel();
	osMutexInit(&mutex);
	osBinSemInit(&sem, 0);
	osMutexTake(&mutex);
	waitAs(2U, &mutex);
	currentPt = &tcbs[1];
	osBinSemTake(&sem);
	CHECK(tcbs[1].state == THREAD_BLOCKED && tcbs[1].waitSem == &sem);
	CHECK(tcbs[1].waitMutex == NULL);
	currentPt = &tcbs[0];
	CHECK(osMutexGive(&mutex));
	CHECK(mutex.owner == &tcbs[2] && tcbs[1].state == THREAD_BLOCKED);
	ready(2U);
	osBinSemGive(&sem);
	ready(1U);
	/* Reject a head whose registered wait relationship does not match. */
	waitAs(0U, &mutex);
	tcbs[0].waitMutex = NULL;
	currentPt = &tcbs[2];
	fakeOsPortFaultExpected = 1;
	if (setjmp(fakeOsPortFaultJump) == 0)
	{
		(void)osMutexGive(&mutex);
		CHECK(0);
	}
	CHECK(osLastBlockingFault.errors == OS_BLOCK_ERROR_WAIT_QUEUE);
	CHECK(mutex.owner == &tcbs[2]);
	CHECK(mutex.waiters.count == 1U && tcbs[0].state == THREAD_BLOCKED);
	CHECK(tcbs[0].waitNode.container == &mutex.waiters);
	puts("PASS: semaphore/mutex isolation and validation before handoff mutation");
}

static void test_repeated_handoff(void)
{
	osMutex mutex;
	resetKernel();
	osMutexInit(&mutex);
	osMutexTake(&mutex);
	for (unsigned int turn = 0U; turn < 1000U; turn++)
	{
		unsigned int owner = turn % 2U;
		unsigned int waiter = 1U - owner;
		waitAs(waiter, &mutex);
		currentPt = &tcbs[owner];
		CHECK(osMutexGive(&mutex));
		CHECK(mutex.owner == &tcbs[waiter]);
		ready(waiter);
		CHECK(osWaitQueueValidate(&mutex.waiters) == OS_WAIT_OK);
	}
	puts("PASS: 1000 ownership transfers with reused TCB nodes");
}

int main(void)
{
	test_ownership_and_fifo_reservation();
	test_self_take_rejected_before_wait();
	test_caller_contract();
	test_mixed_waits_and_bad_handoff();
	test_repeated_handoff();
	printf("All %u mutex host checks passed.\n", checks);
	return EXIT_SUCCESS;
}
