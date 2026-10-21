/* main.c */

#include <stdint.h>
#include <stdio.h>

#include "uart.h"
#include "osKernel.h"
#include "osGpio.h"
#include "osHardwareTest.h"
#include "osPort.h"
#include "osMutex.h"

#define QUANTA              2U
#define PRODUCER_PERIOD_MS  500U
#define LED_PULSE_MS        100U

typedef uint32_t TaskProfiler;

volatile TaskProfiler Task0_Profiler,Task1_Profiler,Task2_Profiler,pTask1_Profiler,pTask2_Profiler;

/* All applications expose thread-creation failure to the debugger. */
volatile uint32_t threadCreationFailed;

#if OS_HARDWARE_TEST_MODE == OS_HW_TEST_SEMAPHORE

/*
 * The event semaphore starts unavailable. The consumer blocks on it until
 * the producer publishes the next periodic event.
 */
static osBinSem eventSem;

/* Volatile counters remain visible in the debugger during the hardware test. */
volatile uint32_t producedSignals;
volatile uint32_t consumedSignals;
volatile uint32_t consumerWaitCalls;

/*-----------------------------------------------------------
 * consumerTask
 *
 * Waits for a producer signal. A successful wake-up increments
 * the consumed count, pulses the Nucleo PA5 user LED, and writes
 * one diagnostic line through USART2 at 115200 baud.
 *----------------------------------------------------------*/
static void consumerTask(void)
{
	while(1)
	{
		/* This count advances before each potentially blocking Take call. */
		consumerWaitCalls++;
		osBinSemTake(&eventSem);

		/* Execution reaches this point only after immediate take or wake-up. */
		consumedSignals++;
		PIN_SET(5);

		printf("Semaphore wake: produced=%lu consumed=%lu time=%lu ms\r\n",
				(unsigned long)producedSignals,
				(unsigned long)consumedSignals,
				(unsigned long)osTimeMs);

		/* Keep the LED pulse visible without busy-waiting. */
		osDelay(LED_PULSE_MS);
		PIN_RESET(5);
	}
}

/*-----------------------------------------------------------
 * producerTask
 *
 * Sleeps for a fixed period, records one produced signal, and
 * gives the binary semaphore. When the consumer is blocked, Give
 * performs direct handoff and requests an immediate reschedule.
 *----------------------------------------------------------*/
static void producerTask(void)
{
	while(1)
	{
		osDelay(PRODUCER_PERIOD_MS);
		producedSignals++;
		osBinSemGive(&eventSem);
	}
}

#elif OS_HARDWARE_TEST_MODE == OS_HW_TEST_SEMAPHORE_FIFO

/*
 * Hardware integration: create A,B,C but stagger the FIRST wait to C,A,B.
 * Subsequent immediate rewaits preserve rotation. Only the producer prints;
 * one outstanding Give at a time makes observed execution identify the
 * selected waiter, rather than confusing FIFO wake selection with the
 * separate round-robin ordering of several simultaneously ready tasks.
 */
static osBinSem fifoSem;
volatile uint32_t fifoWakeCount;
volatile uint32_t fifoLastWake;
volatile uint32_t fifoGiveCount;
volatile uint32_t fifoFailures;

static void fifoWaiter(uint32_t id, uint32_t initialDelay)
{
	osDelay(initialDelay);
	while (1)
	{
		osBinSemTake(&fifoSem);
		osIrqState irqState = osPortEnterCritical();
		fifoLastWake = id;
		fifoWakeCount++;
		osPortMemoryBarrier();
		osPortExitCritical(irqState);
		/* Reuse the same TCB node. It must have been detached by Give. */
	}
}

static void fifoTaskA(void) { fifoWaiter(0U, 50U); }
static void fifoTaskB(void) { fifoWaiter(1U, 100U); }
static void fifoTaskC(void) { fifoWaiter(2U, 0U); }

static uint32_t fifoWaiterCount(void)
{
	/* This diagnostic deliberately inspects queue internals under a lock. */
	osIrqState irqState = osPortEnterCritical();
	uint32_t count = fifoSem.waiters.count;
	osPortExitCritical(irqState);
	return count;
}

static void fifoProducer(void)
{
	const uint32_t expectedOrder[3] = {2U, 0U, 1U};
	while (1)
	{
		/* Wait for all three nodes, including the previous winner's rewait.
		 * A timeout reports a missing waiter instead of hanging silently. */
		uint32_t start = osTimeMs;
		while (fifoWaiterCount() != 3U && (uint32_t)(osTimeMs - start) < 3000U)
		{
			osDelay(1U);
		}
		if (fifoWaiterCount() != 3U)
		{
			fifoFailures++;
			printf("FIFO FAIL: waiters=%lu expected=3 failures=%lu\r\n",
			       (unsigned long)fifoWaiterCount(), (unsigned long)fifoFailures);
			/* Preserve the failed observation; reset to restart this test. */
			while (1) osDelay(1000U);
		}

		osDelay(500U);
		uint32_t expected = expectedOrder[fifoGiveCount % 3U];
		uint32_t before = fifoWakeCount;
		osBinSemGive(&fifoSem);
		fifoGiveCount++;
		start = osTimeMs;
		while (fifoWakeCount == before && (uint32_t)(osTimeMs - start) < 100U)
		{
			osDelay(1U);
		}
		uint32_t passed = (fifoWakeCount == before + 1U) &&
		                  (fifoLastWake == expected) && (fifoSem.value == 0);
		if (!passed) fifoFailures++;
		printf("FIFO %s give=%lu wake=%lu expected=%lu failures=%lu time=%lu ms\r\n",
		       passed ? "PASS" : "FAIL", (unsigned long)fifoGiveCount,
		       (unsigned long)fifoLastWake, (unsigned long)expected,
		       (unsigned long)fifoFailures, (unsigned long)osTimeMs);
		if (fifoGiveCount & 1U) PIN_SET(5);
		else PIN_RESET(5);
	}
}

#elif OS_HARDWARE_TEST_MODE == OS_HW_TEST_COUNTING_SEMAPHORE

/*
 * The consumer delays its first Take so the producer can test accumulation
 * and capacity before testing blocking. Only the producer prints to UART.
 * Counters are debugger-visible; one task writes each counter, while the
 * shared semaphore itself is protected by the normal kernel API.
 */
static osCountSem countSem;
volatile uint32_t countAccepted;
volatile uint32_t countRejected;
volatile uint32_t countConsumed;
volatile uint32_t countFailures;

static void countingConsumer(void)
{
	osDelay(1000U);
	while (1)
	{
		osCountSemTake(&countSem);
		countConsumed++;
		/* Drain all stored tokens, then immediately rewait at zero. */
	}
}

static uint32_t countingWaiters(void)
{
	osIrqState irqState = osPortEnterCritical();
	uint32_t count = countSem.core.waiters.count;
	osPortExitCritical(irqState);
	return count;
}

static void countingProducer(void)
{
	osDelay(100U);
	/* Capacity is three. The consumer is still sleeping, so only the first
	 * three of these five Gives may store a token. Check every return value. */
	for (uint32_t i = 0U; i < 5U; i++)
	{
		if (osCountSemGive(&countSem)) countAccepted++;
		else countRejected++;
	}
	uint32_t passed = (countAccepted == 3U && countRejected == 2U &&
	                   countConsumed == 0U && countSem.core.value == 3);
	if (!passed) countFailures++;
	printf("COUNT burst %s accepted=%lu rejected=%lu consumed=%lu stored=%ld "
	       "failures=%lu\r\n", passed ? "PASS" : "FAIL",
	       (unsigned long)countAccepted, (unsigned long)countRejected,
	       (unsigned long)countConsumed, (long)countSem.core.value,
	       (unsigned long)countFailures);

	while (1)
	{
		/* Consumer must have consumed every accepted token and blocked
		 * again. Timeout detects a missing consumer or failed rewait. */
		uint32_t start = osTimeMs;
		while ((countConsumed != countAccepted || countingWaiters() != 1U) &&
		       (uint32_t)(osTimeMs - start) < 3000U)
		{
			osDelay(1U);
		}
		if (countConsumed != countAccepted || countingWaiters() != 1U)
		{
			countFailures++;
			printf("COUNT FAIL: drain/rewait timeout failures=%lu\r\n",
			       (unsigned long)countFailures);
			while (1) osDelay(1000U);
		}
		osDelay(500U);
		if (osCountSemGive(&countSem)) countAccepted++;
		else countRejected++;
		start = osTimeMs;
		while ((countConsumed != countAccepted || countingWaiters() != 1U) &&
		       (uint32_t)(osTimeMs - start) < 100U)
		{
			osDelay(1U);
		}
		passed = (countConsumed == countAccepted && countRejected == 2U &&
		          countSem.core.value == 0 && countingWaiters() == 1U);
		if (!passed) countFailures++;
		printf("COUNT handoff %s accepted=%lu consumed=%lu rejected=%lu "
		       "stored=%ld waiters=%lu failures=%lu time=%lu ms\r\n",
		       passed ? "PASS" : "FAIL", (unsigned long)countAccepted,
		       (unsigned long)countConsumed, (unsigned long)countRejected,
		       (long)countSem.core.value, (unsigned long)countingWaiters(),
		       (unsigned long)countFailures, (unsigned long)osTimeMs);
		if (countAccepted & 1U) PIN_SET(5);
		else PIN_RESET(5);
	}
}

#elif OS_HARDWARE_TEST_MODE == OS_HW_TEST_MUTEX

static osMutex testMutex;
volatile uint32_t mutexSharedValue;
volatile uint32_t mutexWritesA;
volatile uint32_t mutexWritesB;
volatile uint32_t mutexActiveWorker;
volatile uint32_t mutexFailures;
volatile uint32_t mutexRejectedGives;

static void mutexWorker(uint32_t id)
{
	while (1)
	{
		osMutexTake(&testMutex);
		/* Deliberately sleep while owning the mutex to force the other
		 * worker to block. This is a diagnostic, not recommended lock usage. */
		if (mutexActiveWorker != 0U) mutexFailures++;
		mutexActiveWorker = id;
		uint32_t before = mutexSharedValue;
		osDelay(10U);
		if (mutexActiveWorker != id) mutexFailures++;
		mutexSharedValue = before + 1U;
		if (id == 1U) mutexWritesA++;
		else mutexWritesB++;
		mutexActiveWorker = 0U;
		if (!osMutexGive(&testMutex)) mutexFailures++;
		/* Immediate retake must not steal ownership reserved for a waiter. */
	}
}

static void mutexTaskA(void) { mutexWorker(1U); }
static void mutexTaskB(void) { mutexWorker(2U); }

static bool mutexRejectForeignGive(void)
{
	uint32_t start = osTimeMs;
	while ((uint32_t)(osTimeMs - start) < 100U)
	{
		osIrqState irqState = osPortEnterCritical();
		if (mutexActiveWorker != 0U)
		{
			/* A worker still holds the mutex. Under this snapshot lock it
			 * cannot release between the observation and the foreign Give. */
			bool released = osMutexGive(&testMutex);
			if (released) mutexFailures++;
			else mutexRejectedGives++;
			osPortExitCritical(irqState);
			return !released;
		}
		osPortExitCritical(irqState);
		osYield();
	}
	mutexFailures++;
	return false;
}

static void mutexReporter(void)
{
	uint32_t previousA = 0U;
	uint32_t previousB = 0U;
	uint32_t reports = 0U;
	while (1)
	{
		osDelay(1000U);
		bool rejected = mutexRejectForeignGive();
		osMutexTake(&testMutex);
		/* The reporter also acquires before reading the protected data.
		 * Copy before releasing; UART runs outside the mutex afterward. */
		uint32_t shared = mutexSharedValue;
		uint32_t a = mutexWritesA;
		uint32_t b = mutexWritesB;
		uint32_t passed = rejected && mutexActiveWorker == 0U &&
		                  shared == a + b && a - previousA != 0U &&
		                  b - previousB != 0U && mutexFailures == 0U;
		if (!passed) mutexFailures++;
		uint32_t failures = mutexFailures;
		uint32_t foreignGives = mutexRejectedGives;
		if (!osMutexGive(&testMutex))
		{
			mutexFailures++;
			passed = 0U;
			failures = mutexFailures;
		}
		printf("MUTEX %s shared=%lu A=%lu B=%lu rejected=%lu failures=%lu "
		       "time=%lu ms\r\n", passed ? "PASS" : "FAIL",
		       (unsigned long)shared, (unsigned long)a, (unsigned long)b,
		       (unsigned long)foreignGives, (unsigned long)failures,
		       (unsigned long)osTimeMs);
		previousA = a;
		previousB = b;
		reports++;
		if (reports & 1U) PIN_SET(5);
		else PIN_RESET(5);
	}
}

#elif OS_HARDWARE_TEST_MODE == OS_HW_TEST_STACK_DIAGNOSTICS

volatile uint32_t stackShallowIterations;
volatile uint32_t stackDeepIterations;
volatile uint32_t stackProbeChecksum;
volatile uint32_t stackDiagnosticFailures;

static void stackShallowTask(void)
{
	while (1)
	{
		stackShallowIterations++;
		osDelay(10U);
	}
}

/* Make the workload's additional stack writes explicit even in optimized
 * builds. The array remains live across a delay/context switch. */
__attribute__((noinline)) static void stackDeepProbe(void)
{
	volatile uint32_t scratch[64];
	for (uint32_t i = 0U; i < 64U; i++) scratch[i] = i ^ 0x13579BDFU;
	osDelay(10U);
	stackProbeChecksum = scratch[0] ^ scratch[63];
}

static void stackDeepTask(void)
{
	while (1)
	{
		stackDeepProbe();
		stackDeepIterations++;
	}
}

static void stackReporter(void)
{
	uint32_t previousShallow = 0U;
	uint32_t previousDeep = 0U;
	uint32_t reports = 0U;
	while (1)
	{
		osDelay(1000U);
		bool passed = stackShallowIterations != previousShallow &&
		              stackDeepIterations != previousDeep && stackProbeChecksum == 63U;
		/* This mode creates idle=0, shallow=1, deep=2 and reporter=3.
		 * Slot indexes are diagnostic indexes, not osCreateThread results. */
		for (uint8_t id = 0U; id < 4U; id++)
		{
			osStackUsage usage = {0};
			bool measured = osGetThreadStackUsage(id, &usage);
			bool healthy = measured && usage.guardIntact &&
			               usage.minimumFreeWords > 0U &&
			               usage.allocatedWords == usage.guardWords +
			                   usage.minimumFreeWords + usage.maximumUsedWords;
			if (!healthy) stackDiagnosticFailures++;
			passed = passed && healthy;
			printf("STACK %s task=%u allocated=%lu guard=%lu free=%lu used=%lu "
			       "canary=%s failures=%lu\r\n", healthy ? "PASS" : "FAIL",
			       (unsigned int)id, (unsigned long)usage.allocatedWords,
			       (unsigned long)usage.guardWords, (unsigned long)usage.minimumFreeWords,
			       (unsigned long)usage.maximumUsedWords,
			       usage.guardIntact ? "OK" : "DAMAGED",
			       (unsigned long)stackDiagnosticFailures);
		}
		osStackUsage invalid;
		if (osGetThreadStackUsage(255U, &invalid)) passed = false;
		if (!passed) stackDiagnosticFailures++;
		printf("STACK progress %s shallow=%lu deep=%lu failures=%lu\r\n",
		       passed && stackDiagnosticFailures == 0U ? "PASS" : "FAIL",
		       (unsigned long)stackShallowIterations, (unsigned long)stackDeepIterations,
		       (unsigned long)stackDiagnosticFailures);
		previousShallow = stackShallowIterations;
		previousDeep = stackDeepIterations;
		reports++;
		if (reports & 1U) PIN_SET(5);
		else PIN_RESET(5);
	}
}

#else

/*
 * Each worker owns one volatile counter. A third task observes progress;
 * only that reporter uses UART, avoiding concurrent printf calls.
 * Unsigned subtraction measures progress across counter wraparound.
 */
volatile uint32_t testTaskACount;
volatile uint32_t testTaskBCount;
volatile uint32_t testReportCount;
volatile uint32_t testFailureCount;
volatile uint32_t testSysTickDisabled;

static void testTaskA(void)
{
#if OS_HARDWARE_TEST_MODE == OS_HW_TEST_YIELD
	/*
	 * Startup uses SysTick to get past the initial idle thread. Once A
	 * runs, stop SysTick completely and clear its pending exception.
	 * Protect setup so no tick can intervene before the timer is stopped.
	 * TIM2 remains active for timekeeping and to wake the idle WFI.
	 */
	osIrqState irqState = osPortEnterCritical();
	SysTick->CTRL = 0U;
	/* Clear the counter and any old COUNTFLAG for an unambiguous UART read. */
	SysTick->VAL = 0U;
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
	__DSB();
	__ISB();
	testSysTickDisabled = 1U;
	osPortExitCritical(irqState);
#endif

	while (1)
	{
		testTaskACount++;
#if OS_HARDWARE_TEST_MODE == OS_HW_TEST_YIELD
		/* Every worker handoff must now be requested through PendSV. */
		osYield();
#endif
	}
}

static void testTaskB(void)
{
	while (1)
	{
		testTaskBCount++;
#if OS_HARDWARE_TEST_MODE == OS_HW_TEST_YIELD
		osYield();
#endif
	}
}

static void testReporterTask(void)
{
	uint32_t previousTime = osTimeMs;
	uint32_t previousA = testTaskACount;
	uint32_t previousB = testTaskBCount;

	while (1)
	{
		uint32_t now = osTimeMs;
		if ((uint32_t)(now - previousTime) >= 1000U)
		{
			/* Snapshot related observations without a mid-sample switch. */
			osIrqState irqState = osPortEnterCritical();
			uint32_t countA = testTaskACount;
			uint32_t countB = testTaskBCount;
			uint32_t tickControl = SysTick->CTRL;
			osPortExitCritical(irqState);

			uint32_t deltaA = countA - previousA;
			uint32_t deltaB = countB - previousB;
			uint32_t passed = (deltaA != 0U) && (deltaB != 0U);
#if OS_HARDWARE_TEST_MODE == OS_HW_TEST_YIELD
			/* Exclude a false pass caused by a still-running SysTick. */
			passed = passed && testSysTickDisabled &&
				((tickControl & (SysTick_CTRL_ENABLE_Msk |
				                 SysTick_CTRL_TICKINT_Msk)) == 0U);
#else
			passed = passed &&
				((tickControl & (SysTick_CTRL_ENABLE_Msk |
				                 SysTick_CTRL_TICKINT_Msk)) ==
				 (SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk));
#endif
			testReportCount++;
			if (!passed)
			{
				testFailureCount++;
			}

			printf("PendSV mode=%u %s A=%lu B=%lu dA=%lu dB=%lu "
			       "tick=0x%lx failures=%lu time=%lu ms\r\n",
			       (unsigned int)OS_HARDWARE_TEST_MODE,
			       passed ? "PASS" : "FAIL",
			       (unsigned long)countA, (unsigned long)countB,
			       (unsigned long)deltaA, (unsigned long)deltaB,
			       (unsigned long)tickControl,
			       (unsigned long)testFailureCount, (unsigned long)now);

			/* Toggle the LED once per report without a blocking delay. */
			if (testReportCount & 1U)
			{
				PIN_SET(5);
			}
			else
			{
				PIN_RESET(5);
			}
			previousA = countA;
			previousB = countB;
			previousTime = now;
		}
#if OS_HARDWARE_TEST_MODE == OS_HW_TEST_YIELD
		/* No diagnostic task may depend on osDelay with SysTick stopped. */
		osYield();
#endif
	}
}

#endif /* OS_HARDWARE_TEST_MODE */

int main(void)
{
	/* Initialize GPIO before tasks begin driving the user LED. */
	gpio_init();
	PIN_RESET(5);

	/* Initialize USART2 TX for the ST-LINK virtual COM port. */
	uart_tx_init();

	/* Initialize the independent 1 ms kernel time base. */
	tim2_1Khz_interrupt_init();

#if OS_HARDWARE_TEST_MODE == OS_HW_TEST_SEMAPHORE
	/* Start unavailable so the first consumer Take must block. */
	osBinSemInit(&eventSem, 0);
#elif OS_HARDWARE_TEST_MODE == OS_HW_TEST_SEMAPHORE_FIFO
	osBinSemInit(&fifoSem, 0);
#elif OS_HARDWARE_TEST_MODE == OS_HW_TEST_COUNTING_SEMAPHORE
	if (!osCountSemInit(&countSem, 3U, 0U))
	{
		/* Invalid static test parameters must prevent a partial launch. */
		printf("COUNT FAIL: semaphore initialization rejected.\r\n");
		while (1) __WFI();
	}
#elif OS_HARDWARE_TEST_MODE == OS_HW_TEST_MUTEX
	osMutexInit(&testMutex);
#endif

	/* Initialize the kernel and its idle thread. */
	osInit();

	/* Record thread-creation failure instead of launching a partial test. */
#if OS_HARDWARE_TEST_MODE == OS_HW_TEST_SEMAPHORE
	uint32_t created = osCreateThread(consumerTask) &&
	                   osCreateThread(producerTask);
#elif OS_HARDWARE_TEST_MODE == OS_HW_TEST_SEMAPHORE_FIFO
	uint32_t created = osCreateThread(fifoTaskA) && osCreateThread(fifoTaskB) &&
	                   osCreateThread(fifoTaskC) && osCreateThread(fifoProducer);
#elif OS_HARDWARE_TEST_MODE == OS_HW_TEST_COUNTING_SEMAPHORE
	uint32_t created = osCreateThread(countingConsumer) &&
	                   osCreateThread(countingProducer);
#elif OS_HARDWARE_TEST_MODE == OS_HW_TEST_MUTEX
	uint32_t created = osCreateThread(mutexTaskA) && osCreateThread(mutexTaskB) &&
	                   osCreateThread(mutexReporter);
#elif OS_HARDWARE_TEST_MODE == OS_HW_TEST_STACK_DIAGNOSTICS
	uint32_t created = osCreateThread(stackShallowTask) && osCreateThread(stackDeepTask) &&
	                   osCreateThread(stackReporter);
#else
	uint32_t created = osCreateThread(testTaskA) &&
	                   osCreateThread(testTaskB) &&
	                   osCreateThread(testReporterTask);
#endif
	if (!created)
	{
		threadCreationFailed = 1U;
		while(1)
		{
			__WFI();
		}
	}

#if OS_HARDWARE_TEST_MODE == OS_HW_TEST_SEMAPHORE
	printf("Binary semaphore hardware test started.\r\n");
#elif OS_HARDWARE_TEST_MODE == OS_HW_TEST_SEMAPHORE_FIFO
	printf("Semaphore FIFO test: created A,B,C; expected wake 2,0,1 repeatedly.\r\n");
#elif OS_HARDWARE_TEST_MODE == OS_HW_TEST_COUNTING_SEMAPHORE
	printf("Counting semaphore test: capacity=3, burst=5, then direct handoff.\r\n");
#elif OS_HARDWARE_TEST_MODE == OS_HW_TEST_MUTEX
	printf("Basic mutex test: two contending workers and owner-only Give.\r\n");
#elif OS_HARDWARE_TEST_MODE == OS_HW_TEST_STACK_DIAGNOSTICS
	printf("Stack diagnostics: words=400 guard=8; idle=0 shallow=1 deep=2 reporter=3.\r\n");
#elif OS_HARDWARE_TEST_MODE == OS_HW_TEST_PREEMPTION
	printf("PendSV preemption test: busy tasks, no yield or delay.\r\n");
#else
	printf("PendSV yield test: task A stops SysTick after startup.\r\n");
#endif

	/* Launch preemptive round-robin scheduling with a 2 ms quantum. */
	osKernelLaunch(QUANTA);
}


void TIM2_IRQHandler(void)
{
	if(TIM2->SR & SR_UIF)
	{
		/*Clear update interrupt flag*/
		TIM2->SR &=~SR_UIF;

		/*Do something*/
		pTask2_Profiler++;

		osKernelTick();
	}

}
