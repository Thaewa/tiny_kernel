/* osKernel.c */

#include "osKernel.h"
#include "osKernelInternal.h"
#include "osBlocking.h"
#include "osKernelPrivate.h"
#include "cmsis_gcc.h"

#define MAX_THREADS OS_MAX_THREADS

#define BUS_FREQ				16000000

#define CTRL_ENABLE		(1U<<0)
#define CTRL_TICKINT	(1U<<1)
#define CTRL_CLCKSRC	(1U<<2)
#define CTRL_COUNTFLAG	(1U<<16)
#define SYSTICK_RST		0

#define TIM2EN			(1U<<0)
#define CR1_CEN			(1U<<0)
#define DIER_UIE		(1U<<0)

//uint32_t period_tick;


void osSchedulerBootStrap(void);
void osScheduler(void);

uint32_t 	MILLIS_PRESCALER;

tcbType tcbs[MAX_THREADS];
tcbType *currentPt = 0;
volatile uint8_t thread_count = 0;

volatile uint32_t osTimeMs = 0;
volatile uint32_t osKernelRunning;

/* Returning is not deletion: keep the task/stack alive as before. */
void osThreadExit(void)
{
	for (;;) __WFI();
}

/*-----------------------------------------------------------
 * osWakeSleeping
 *
 * Checks all threads and wakes those whose sleep period has expired.
 * Changes their state from THREAD_SLEEPING to THREAD_READY.
 *----------------------------------------------------------*/
static void osWakeSleeping(void)
{
	/* Iterate through all threads */
	for(int i = 0; i < thread_count; i++)
	{
		/* If thread is sleeping and wake time has passed */
		if((tcbs[i].state              == THREAD_SLEEPING) &&
		   ((int32_t)(osTimeMs - tcbs[i].wake_at_ms) >= 0))
		{
			/* Mark thread as ready to run */
			tcbs[i].state = THREAD_READY;
		}
	}
}

/*-----------------------------------------------------------
 * osKernelTick
 *
 * Called from TIM2_IRQHandler in main.c.
 * Increments the global kernel tick counter
 * and wakes any sleeping threads whose time has expired.
 *----------------------------------------------------------*/
void osKernelTick(void)
{
    /* Increment kernel global time (milliseconds) */
    osTimeMs++;

    /* Wake up sleeping threads whose delay has ended */
    osWakeSleeping();
}


/*-----------------------------------------------------------
 * idle_task
 *
 * Default idle thread that runs when no other thread is ready.
 * It continuously waits for interrupts to save CPU power.
 *----------------------------------------------------------*/
static void idle_task(void)
{
	/* Infinite low-power idle loop */
	while(1)
	{
		__WFI();

		/*
		 * Offer the CPU to ready threads after an interrupt wakes idle.
		 * This also permits cooperative scheduling with SysTick stopped:
		 * TIM2 wakes WFI, then this explicit yield requests PendSV.
		 */
		osYield();
	}
}

/*-----------------------------------------------------------
 * osInit
 *
 * Initializes kernel parameters and creates the idle task.
 * Ensures that at least one thread (idle) is always READY.
 *----------------------------------------------------------*/
void osInit(void)
{
	/* Set prescaler for 1ms tick based on system clock */
	MILLIS_PRESCALER  = (BUS_FREQ/1000);

	/* Create idle task to guarantee one READY thread */
	osCreateThread(idle_task);
}

/*-----------------------------------------------------------
 * osKernelLaunch
 *
 * Configures and starts the system timer (SysTick),
 * sets up scheduler parameters, and launches the first thread.
 *----------------------------------------------------------*/
void osKernelLaunch(uint32_t quanta)
{
	__disable_irq();

	/*Reset systick*/
	SysTick->CTRL = 0;

	SysTick->LOAD = (quanta * MILLIS_PRESCALER) - 1;

	/*Clear systick current value register*/
	SysTick->VAL = 0;

	/*
	 * Keep both exceptions at the lowest implemented priority. PendSV must
	 * not preempt SysTick or another handler: this port switches the MSP,
	 * so the saved frame must belong to a thread, not a nested ISR.
	 * Equal priorities prevent either exception from preempting the other;
	 * PendSV can run after SysTick returns, including by tail-chaining.
	 */
	NVIC_SetPriority(PendSV_IRQn, 15);
	NVIC_SetPriority(SysTick_IRQn, 15);

	/* Discard stale scheduling requests before the first thread starts. */
	SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk | SCB_ICSR_PENDSTCLR_Msk;

	/*Enable systick interrupt*/
	//SysTick->CTRL  |= CTRL_TICKINT;
	SysTick->CTRL = CTRL_CLCKSRC | CTRL_TICKINT | CTRL_ENABLE;

	/*Launch scheduler*/
	/* Interrupts remain masked until bootstrap selects the first stack. */
	osKernelRunning = 1U;
	osSchedulerBootStrap();
}


/*-----------------------------------------------------------
 * SysTick_Handler
 *
 * Requests round-robin scheduling when the time quantum expires.
 * TIM2 remains responsible for the independent millisecond time base.
 * Returning normally lets PendSV receive a thread's exception frame;
 * this handler must not save or restore a task stack itself.
 *----------------------------------------------------------*/
void SysTick_Handler(void)
{
	osYield();
}

/*-----------------------------------------------------------
 * PendSV_Handler
 *
 * Performs the deferred context switch requested by SysTick or a thread.
 * At the lowest exception priority it cannot preempt another handler.
 * The existing port uses MSP for thread stacks and exception frames;
 * R4-R11 are saved in software, while the CPU saves the basic frame.
 * Interrupts protect currentPt and stack selection during the switch.
 *
 * This milestone retains the basic integer context of the original
 * port. Saving floating-point task context is not implemented here.
 *----------------------------------------------------------*/
/* When exception occurs these registers are automatically
 * saved onto the stack: r0, r1, r2, r3, r12, lr, pc, xPSR.
 */
__attribute__((naked)) void PendSV_Handler(void)
{
	__asm volatile(
	/*SUSPEND CURRENT THREAD*/

	/*Disable global interrupts*/
	"CPSID	I				\n"
	/*Save r4,r5,r6,r7,r8,r9,r10,11*/
	"PUSH	{R4-R11}		\n"

	/*Load address of currentPt into r0*/
	"LDR	R0, =currentPt	\n"

	/*Load r1 from address equals r0, i.e. r1 =currentPt*/
	"LDR	R1,[R0]			\n"

	/*Store Cortex-M SP at address equals r1, i.e Save SP into tcb */
	"STR	SP,[R1]			\n"

	/*CHOOSE NEXT THREAD*/
	"PUSH	{R0,LR}			\n"
	"BL		osScheduler		\n"
	"POP	{R0,LR}			\n"

	/*R1 =  currentPt i.e. New Thread*/
	"LDR	R1,[R0]			\n"

	/*SP  = currentPt->StackPt*/
	"LDR	SP,[R1]			\n"

	/*Restore r4,r5,r6,r7,r8,r9,r10,11*/
	"POP	{R4-R11}		\n"

	/*Enable global interrupts*/
	"CPSIE	I				\n"

	/*Return from exception and restore r0,r1,r2,r3,r12,lr,pc,psr */
	"BX		LR				\n"
	);
}

/*-----------------------------------------------------------
 * osSchedulerBootStrap
 *
 * Bootstrap function to start the first task after kernel launch.
 * This function manually restores the initial stack frame for the
 * very first thread without using an exception return sequence.
 *
 * It avoids simultaneous POP of LR and PC by loading them separately,
 * ensuring proper control flow when switching to the first thread.
 *----------------------------------------------------------*/
__attribute__((naked)) void osSchedulerBootStrap(void)
{
    __asm volatile(
        "LDR R0,=currentPt      \n"	/* Load address of currentPt */
        "LDR R2,[R0]            \n"	/* Load currentPt value (pointer to TCB) */
        "LDR SP,[R2]            \n"	/* Set stack pointer to the task’s stack */

    	/* Restore software-saved registers */
        "POP {R4-R11}           \n"

    	/* Restore hardware-like frame step by step (no xPSR like exception-return) */
        "POP {R0-R3,R12}        \n"	/* Restore R0–R3 and R12 */
        "LDR LR, [SP], #4       \n"	/* Restore LR one word at a time (avoid POP LR) */
        "LDR R3, [SP], #4       \n"	/* Load PC into R3 */
        "ADD SP, SP, #4         \n"	/* Skip xPSR (not used here) */
        "CPSIE I                \n"	/* Enable interrupts */
        "BX R3                  \n"	/* Branch to task entry point */
    );
}

/*-----------------------------------------------------------
 * osYield
 *
 * Requests a deferred context switch by pending the PendSV exception.
 * A direct write sets only the requested ICSR command bit; no read-modify-
 * write is needed. Multiple requests before service coalesce into one.
 * SysTick's counter is left running, so yielding does not restart the
 * periodic scheduling timer.
 *
 * The barriers complete the request before subsequent instructions.
 * They do not unmask interrupts: if the caller masks PendSV, the switch
 * stays pending. Blocking callers must allow scheduling to proceed.
 *----------------------------------------------------------*/
void osYield(void)
{
	SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
	__DSB();
	__ISB();
}

/*-----------------------------------------------------------
 * osScheduler
 *
 * Implements a simple Round-Robin scheduler.
 * It moves the current thread pointer to the next ready thread,
 * skipping any threads that are BLOCKED or SLEEPING.
 *----------------------------------------------------------*/
void osScheduler(void)
{
	/* Initialize attempt counter equal to total thread count */
	int tries = thread_count;

    do
    {
    	/* Move to the next thread in the circular linked list */
        currentPt = currentPt->nextPt;
    }
    while((--tries)                                                                    &&
    	  ((currentPt->state == THREAD_BLOCKED) || (currentPt->state == THREAD_SLEEPING)));
}

/*-----------------------------------------------------------
 * tim2_1Khz_interrupt_init
 *
 * Initializes TIM2 to generate a 1 kHz (1 ms period) interrupt.
 * This timer is typically used as the kernel tick source.
 *----------------------------------------------------------*/
void tim2_1Khz_interrupt_init(void)
{
	/*Enable clock access to tim2*/
	RCC->APB1ENR |=TIM2EN;

	/*Set timer prescaler*/
	TIM2->PSC =  1600 - 1; // 16 MHz / 1600 = 10 kHz

	/*Set auto-reload value*/
	TIM2->ARR =  10 -1;   //10 kHz / 10 = 1 kHz → 1 ms

	/*Clear counter*/
	TIM2->CNT = 0;

	/*Enable timer interrupt*/
	TIM2->DIER |= DIER_UIE;

    /*Set priorities BEFORE enabling IRQs*/
    NVIC_SetPriority(TIM2_IRQn, 14);   // TIM2 สูงกว่า SysTick เล็กน้อย

	/*Enable timer interrupt in NVIC*/
	NVIC_EnableIRQ(TIM2_IRQn);

	/*Enable timer*/
	TIM2->CR1 =  CR1_CEN;
}

/*-----------------------------------------------------------
 * osKernelRequestContextSwitch
 *
 * Provides a portable scheduling request to synchronization
 * primitives. osYield() pends PendSV, keeping exception registers and
 * assembly context switching outside the portable semaphore module.
 *----------------------------------------------------------*/
void osKernelRequestContextSwitch(void)
{
	osYield();
}

/*-----------------------------------------------------------
 * osDelay
 *
 * Puts the current thread to sleep for the specified number of milliseconds.
 * The scheduler will not run this thread again until osTimeMs >= wake_at_ms.
 *----------------------------------------------------------*/
void osDelay(uint32_t ms)
{
    /* Even delay(0) currently sleeps/yields, so it requires valid context. */
    osExecutionContext caller = osPortGetExecutionContext();
    uint32_t errors = osBlockingContextErrors(caller);
    if (errors != 0U)
    {
        osPortBlockingFault(errors, OS_BLOCK_OPERATION_DELAY, caller);
    }

    osIrqState irqState = osPortEnterCritical();

    /* Calculate absolute wake-up time */
    currentPt->wake_at_ms = osTimeMs + ms;

    /* Mark current thread as sleeping */
    currentPt->state = THREAD_SLEEPING;

    /* Publish sleeping state and pend PendSV before restoring interrupts.
     * An ISR cannot observe an incomplete sleep/scheduling transition. */
    osPortMemoryBarrier();
    osYield();
    osPortExitCritical(irqState);
}
