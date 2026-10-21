#ifndef __OSKERNEL_H__
#define __OSKERNEL_H__
#include <stdint.h>
#include <osSemaphore.h>
#include "osStack.h"
#include "stm32f4xx.h"

#define PERIOD 100   /*Period is 100*quanta, e.g. quanta =10-> 100*10 = 1000*/
#define SR_UIF		(1U<<0)

void tim2_1Khz_interrupt_init(void);


void osInit(void);
void osKernelLaunch(uint32_t quanta);
uint8_t osCreateThread(void(*task)(void));
/* Diagnostic slot index: idle=0, then thread creation order. Not a task
 * handle. Call from task/startup context; ISR calls/invalid slots return
 * false without changing result. Inspect result->guardIntact separately. */
bool osGetThreadStackUsage(uint8_t threadIndex, osStackUsage *result);
void osYield(void);
void task3(void);

void osKernelTick(void);
/* Task-only; scheduling must be unmasked, including for ms == 0. */
void osDelay(uint32_t ms);

extern volatile uint32_t osTimeMs;

#endif
