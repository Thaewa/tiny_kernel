#ifndef __OSKERNEL_H__
#define __OSKERNEL_H__
#include <stdint.h>
#include "stm32f4xx.h"

#define PERIOD 100   /*Period is 100*quanta, e.g. quanta =10-> 100*10 = 1000*/
#define SR_UIF		(1U<<0)

void tim2_1Khz_interrupt_init(void);


void osInit(void);
void osKernelLaunch(uint32_t quanta);
uint8_t osCreateThread(void(*task)(void));
void osYield(void);
void task3(void);

typedef struct
{
	volatile int32_t value;   /* "0" or "1" only */
} osBinSem;

void osBinSemInit(osBinSem *sem, int32_t initial);
void osBinSemTake(osBinSem *sem);
void osBinSemGive(osBinSem *sem);

void osKernelTick(void);
void osDelay(uint32_t ms);

extern volatile uint32_t osTimeMs;

#endif
