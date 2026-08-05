/* main.c */

#include <stdint.h>
#include <stdio.h>

#include "uart.h"
#include "osKernel.h"
#include "osGpio.h"

#define QUANTA              2U
#define PRODUCER_PERIOD_MS  500U
#define LED_PULSE_MS        100U

typedef uint32_t TaskProfiler;

volatile TaskProfiler Task0_Profiler,Task1_Profiler,Task2_Profiler,pTask1_Profiler,pTask2_Profiler;

/*
 * The event semaphore starts unavailable. The consumer blocks on it until
 * the producer publishes the next periodic event.
 */
static osBinSem eventSem;

/* Volatile counters remain visible in the debugger during the hardware test. */
volatile uint32_t producedSignals;
volatile uint32_t consumedSignals;
volatile uint32_t consumerWaitCalls;
volatile uint32_t threadCreationFailed;

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

int main(void)
{
	/* Initialize GPIO before tasks begin driving the user LED. */
	gpio_init();
	PIN_RESET(5);

	/* Initialize USART2 TX for the ST-LINK virtual COM port. */
	uart_tx_init();

	/* Initialize the independent 1 ms kernel time base. */
	tim2_1Khz_interrupt_init();

	/* Start unavailable so the first consumer Take must block. */
	osBinSemInit(&eventSem, 0);

	/* Initialize the kernel and its idle thread. */
	osInit();

	/* Record thread-creation failure instead of launching a partial test. */
	if ((!osCreateThread(consumerTask)) || (!osCreateThread(producerTask)))
	{
		threadCreationFailed = 1U;
		while(1)
		{
			__WFI();
		}
	}

	printf("Binary semaphore hardware test started.\r\n");

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
