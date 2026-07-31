/* main.c */

#include <osGpio.h>
#include "uart.h"
#include "osKernel.h"
#include "osGpio.h"

#define		QUANTA		2

typedef uint32_t TaskProfiler;

volatile TaskProfiler Task0_Profiler,Task1_Profiler,Task2_Profiler,pTask1_Profiler,pTask2_Profiler;

void motor_run(void);
void motor_stop(void);
void valve_open(void);
void valve_close(void);

static osBinSem sem1, sem2;

void task0(void)
{
	while(1)
	{
        PIN_SET(4);
        osDelay(1);     // 1 ms delay (kernel tick)
        PIN_RESET(4);
        osDelay(1);
	}
}

void task1(void)
{
	while(1)
	{
        PIN_SET(5);
        osDelay(1);     // 1 ms delay (kernel tick)
        PIN_RESET(5);
        osDelay(1);
	}
}

void task2(void)
{
	while(1)
	{
        PIN_SET(6);
        osDelay(1);     // 1 ms delay (kernel tick)
        PIN_RESET(6);
        osDelay(1);
	}
}

void task3(void)
{
	while(1)
	{
        PIN_SET(7);
        osDelay(1);     // 1 ms delay (kernel tick)
        PIN_RESET(7);
        osDelay(1);
	}
}

int main(void)
{

	/*Initialize gpio*/
	gpio_init ();

    /*Initialize uart*/
	uart_tx_init();

    /*Initialize hardware timer*/
	tim2_1Khz_interrupt_init();

	/*Initialize semaphores*/
	osBinSemInit(&sem1,1);
	osBinSemInit(&sem2,0);


	/*Initialize Kernel*/
	osInit();

	/*Add Threads*/
	osCreateThread(&task0);
	osCreateThread(&task1);
	osCreateThread(&task2);
	osCreateThread(&task3);
	/*Set RoundRobin time quanta*/
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
