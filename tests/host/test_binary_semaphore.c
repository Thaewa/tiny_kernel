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

int main(void)
{
	/* Execute the initialization contract tests in plan order. */
	test_init_available();
	test_init_unavailable();
	test_init_clamps_positive_value();
	test_init_clamps_negative_value();
	test_take_available();

	/* Returning success makes the CI job green when every check passes. */
	printf("All %u binary semaphore host tests passed.\n", tests_run);
	return EXIT_SUCCESS;
}
