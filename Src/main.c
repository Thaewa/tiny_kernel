/* main.c */

#include <stdint.h>
#include <stdio.h>

#include "uart.h"
#include "osKernel.h"
#include "osGpio.h"
#include "osHardwareTest.h"
#include "osPort.h"

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
#endif

	/* Initialize the kernel and its idle thread. */
	osInit();

	/* Record thread-creation failure instead of launching a partial test. */
#if OS_HARDWARE_TEST_MODE == OS_HW_TEST_SEMAPHORE
	uint32_t created = osCreateThread(consumerTask) &&
	                   osCreateThread(producerTask);
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
