#include <stdio.h>
#include <stdlib.h>

#include "osSemaphore.h"
#include "fake_os_kernel.h"
#include "fake_os_port.h"

/* Number of assertions that completed successfully in this test process. */
static unsigned int tests_run;

/*-----------------------------------------------------------
 * fail
 *
 * Prints a useful diagnostic and terminates the test process.
 * A non-zero process exit status causes GitHub Actions to mark
 * the Host Tests workflow as failed.
 *----------------------------------------------------------*/
static void fail(const char *test_name, int32_t expected, int32_t actual)
{
	fprintf(stderr,
			"FAIL: %s (expected %ld, got %ld)\n",
			test_name,
			(long)expected,
			(long)actual);
	exit(EXIT_FAILURE);
}

/*-----------------------------------------------------------
 * expect_value
 *
 * Checks the observable value stored in a binary semaphore.
 * Each successful check is counted and printed so the CI log
 * identifies exactly which contract cases were executed.
 *----------------------------------------------------------*/
static void expect_value(const char *test_name,
						 int32_t expected,
						 const osBinSem *sem)
{
	tests_run++;
	if (sem->value != expected)
	{
		fail(test_name, expected, sem->value);
	}

	printf("PASS: %s\n", test_name);
}

/*-----------------------------------------------------------
 * expect_count
 *
 * Checks an unsigned fake-service call count or saved state.
 * Separate value and count helpers keep diagnostics readable
 * without introducing an external unit-test framework.
 *----------------------------------------------------------*/
static void expect_count(const char *test_name,
						 uint32_t expected,
						 uint32_t actual)
{
	tests_run++;
	if (actual != expected)
	{
		fprintf(stderr,
				"FAIL: %s (expected %lu, got %lu)\n",
				test_name,
				(unsigned long)expected,
				(unsigned long)actual);
		exit(EXIT_FAILURE);
	}

	printf("PASS: %s\n", test_name);
}

/*-----------------------------------------------------------
 * expect_pointer
 *
 * Confirms that an internal kernel service received the exact
 * object supplied by the semaphore operation. Pointer identity
 * matters because a thread must be linked to the semaphore on
 * which it actually attempted to wait.
 *----------------------------------------------------------*/
static void expect_pointer(const char *test_name,
						   const void *expected,
						   const void *actual)
{
	tests_run++;
	if (actual != expected)
	{
		fprintf(stderr,
				"FAIL: %s (expected %p, got %p)\n",
				test_name,
				expected,
				actual);
		exit(EXIT_FAILURE);
	}

	printf("PASS: %s\n", test_name);
}

/* BS-01: An initial value of one makes the semaphore available. */
static void test_init_available(void)
{
	/* Deliberately leave the local object uninitialized first. */
	osBinSem sem;

	/* Initialize the semaphore in the available state. */
	osBinSemInit(&sem, 1);

	/* The public state must contain the binary value one. */
	expect_value("BS-01 init with one", 1, &sem);
}

/* BS-02: An initial value of zero makes the semaphore unavailable. */
static void test_init_unavailable(void)
{
	osBinSem sem;

	/* Zero represents that no semaphore signal is available. */
	osBinSemInit(&sem, 0);

	expect_value("BS-02 init with zero", 0, &sem);
}

/* BS-03: Values above the binary range are clamped to one. */
static void test_init_clamps_positive_value(void)
{
	osBinSem sem;

	/* A binary semaphore must not retain a counting value such as 99. */
	osBinSemInit(&sem, 99);

	expect_value("BS-03 clamp positive value", 1, &sem);
}

/* BS-04: Negative values are clamped to the unavailable state. */
static void test_init_clamps_negative_value(void)
{
	osBinSem sem;

	/* Negative counts are invalid and therefore normalize to zero. */
	osBinSemInit(&sem, -1);

	expect_value("BS-04 clamp negative value", 0, &sem);
}

/*-----------------------------------------------------------
 * BS-05: Taking an available semaphore succeeds immediately.
 *
 * Besides checking the value transition from one to zero, this
 * test verifies the interaction contract around the portable
 * logic: the critical section is balanced, the saved interrupt
 * state is restored, and no block or context-switch request is
 * issued on the successful fast path.
 *----------------------------------------------------------*/
static void test_take_available(void)
{
	osBinSem sem;

	/* Start each interaction test with empty fake call histories. */
	fakeOsPortReset();
	fakeOsKernelReset();
	osBinSemInit(&sem, 1);

	/* Exercise the production implementation compiled for the host. */
	osBinSemTake(&sem);

	expect_value("BS-05 available take consumes signal", 0, &sem);
	expect_count("BS-05 enters critical section", 1U,
				 fakeOsPortEnterCriticalCallCount());
	expect_count("BS-05 exits critical section", 1U,
				 fakeOsPortExitCriticalCallCount());
	expect_count("BS-05 executes memory barrier", 1U,
				 fakeOsPortMemoryBarrierCallCount());
	expect_count("BS-05 restores saved interrupt state",
				 FAKE_OS_PORT_SAVED_IRQ_STATE,
				 fakeOsPortLastRestoredIrqState());
	expect_count("BS-05 does not block current thread", 0U,
				 fakeOsKernelBlockCallCount());
	expect_count("BS-05 does not request context switch", 0U,
				 fakeOsKernelContextSwitchCallCount());
}

/*-----------------------------------------------------------
 * BS-06: Taking an unavailable semaphore blocks the caller.
 *
 * The semaphore value must remain zero while the kernel records
 * the current thread's wait relationship. After shared state is
 * published and the saved interrupt state is restored, exactly
 * one context-switch request must be issued so another runnable
 * thread can execute.
 *----------------------------------------------------------*/
static void test_take_unavailable_blocks(void)
{
	osBinSem sem;

	/* Begin with no recorded calls and no available semaphore signal. */
	fakeOsPortReset();
	fakeOsKernelReset();
	osBinSemInit(&sem, 0);

	/* Exercise the blocking path of the production implementation. */
	osBinSemTake(&sem);

	expect_value("BS-06 unavailable value remains zero", 0, &sem);
	expect_count("BS-06 enters critical section", 1U,
				 fakeOsPortEnterCriticalCallCount());
	expect_count("BS-06 exits critical section", 1U,
				 fakeOsPortExitCriticalCallCount());
	expect_count("BS-06 executes memory barrier", 1U,
				 fakeOsPortMemoryBarrierCallCount());
	expect_count("BS-06 restores saved interrupt state",
				 FAKE_OS_PORT_SAVED_IRQ_STATE,
				 fakeOsPortLastRestoredIrqState());
	expect_count("BS-06 blocks current thread once", 1U,
				 fakeOsKernelBlockCallCount());
	expect_pointer("BS-06 blocks on requested semaphore", &sem,
				   fakeOsKernelLastBlockedSemaphore());
	expect_count("BS-06 requests one context switch", 1U,
				 fakeOsKernelContextSwitchCallCount());
}

/*-----------------------------------------------------------
 * BS-07: Giving with no waiter stores one available signal.
 *
 * The kernel unblock service must still be queried while the
 * critical section is active. When it reports no waiter, Give
 * sets the binary value to one and returns without scheduling.
 *----------------------------------------------------------*/
static void test_give_without_waiter_makes_available(void)
{
	osBinSem sem;

	fakeOsPortReset();
	fakeOsKernelReset();
	osBinSemInit(&sem, 0);

	osBinSemGive(&sem);

	expect_value("BS-07 give stores available signal", 1, &sem);
	expect_count("BS-07 enters critical section", 1U,
				 fakeOsPortEnterCriticalCallCount());
	expect_count("BS-07 exits critical section", 1U,
				 fakeOsPortExitCriticalCallCount());
	expect_count("BS-07 executes memory barrier", 1U,
				 fakeOsPortMemoryBarrierCallCount());
	expect_count("BS-07 restores saved interrupt state",
				 FAKE_OS_PORT_SAVED_IRQ_STATE,
				 fakeOsPortLastRestoredIrqState());
	expect_count("BS-07 checks for one waiter", 1U,
				 fakeOsKernelUnblockCallCount());
	expect_pointer("BS-07 checks requested semaphore", &sem,
				   fakeOsKernelLastUnblockedSemaphore());
	expect_count("BS-07 does not request context switch", 0U,
				 fakeOsKernelContextSwitchCallCount());
}

/*-----------------------------------------------------------
 * BS-08: Repeated gives remain binary when no waiter exists.
 *
 * Two Give calls exercise two complete protected operations,
 * but the stored value must remain one rather than becoming a
 * counting semaphore value of two.
 *----------------------------------------------------------*/
static void test_repeated_give_coalesces_signal(void)
{
	osBinSem sem;

	fakeOsPortReset();
	fakeOsKernelReset();
	osBinSemInit(&sem, 1);

	osBinSemGive(&sem);
	osBinSemGive(&sem);

	expect_value("BS-08 repeated give remains binary", 1, &sem);
	expect_count("BS-08 enters critical section twice", 2U,
				 fakeOsPortEnterCriticalCallCount());
	expect_count("BS-08 exits critical section twice", 2U,
				 fakeOsPortExitCriticalCallCount());
	expect_count("BS-08 executes two memory barriers", 2U,
				 fakeOsPortMemoryBarrierCallCount());
	expect_count("BS-08 checks waiters twice", 2U,
				 fakeOsKernelUnblockCallCount());
	expect_count("BS-08 does not request context switch", 0U,
				 fakeOsKernelContextSwitchCallCount());
}

/*-----------------------------------------------------------
 * BS-09: Giving with a waiter performs direct handoff.
 *
 * The fake kernel reports that one waiter became ready. Give
 * must therefore leave the stored value at zero, publish the
 * handoff, restore the interrupt state, and request one context
 * switch for the newly readied thread.
 *----------------------------------------------------------*/
static void test_give_with_waiter_uses_direct_handoff(void)
{
	osBinSem sem;

	fakeOsPortReset();
	fakeOsKernelReset();
	fakeOsKernelSetUnblockResult(true);
	osBinSemInit(&sem, 0);

	osBinSemGive(&sem);

	expect_value("BS-09 direct handoff keeps value zero", 0, &sem);
	expect_count("BS-09 enters critical section", 1U,
				 fakeOsPortEnterCriticalCallCount());
	expect_count("BS-09 exits critical section", 1U,
				 fakeOsPortExitCriticalCallCount());
	expect_count("BS-09 executes memory barrier", 1U,
				 fakeOsPortMemoryBarrierCallCount());
	expect_count("BS-09 restores saved interrupt state",
				 FAKE_OS_PORT_SAVED_IRQ_STATE,
				 fakeOsPortLastRestoredIrqState());
	expect_count("BS-09 unblocks one waiter", 1U,
				 fakeOsKernelUnblockCallCount());
	expect_pointer("BS-09 hands off requested semaphore", &sem,
				   fakeOsKernelLastUnblockedSemaphore());
	expect_count("BS-09 requests one context switch", 1U,
				 fakeOsKernelContextSwitchCallCount());
}

int main(void)
{
	/* Execute the initialization contract tests in plan order. */
	test_init_available();
	test_init_unavailable();
	test_init_clamps_positive_value();
	test_init_clamps_negative_value();
	test_take_available();
	test_take_unavailable_blocks();
	test_give_without_waiter_makes_available();
	test_repeated_give_coalesces_signal();
	test_give_with_waiter_uses_direct_handoff();

	/* Returning success makes the CI job green when every check passes. */
	printf("All %u binary semaphore host checks passed.\n", tests_run);
	return EXIT_SUCCESS;
}
