/* osKernel.c */

#include "osKernel.h"
#include "osKernelInternal.h"
#include "cmsis_gcc.h"

//#define NUM_OF_THREADS			3
#define STACKSIZE				400
#define MAX_THREADS   8

#define BUS_FREQ				16000000

#define CTRL_ENABLE		(1U<<0)
#define CTRL_TICKINT	(1U<<1)
#define CTRL_CLCKSRC	(1U<<2)
#define CTRL_COUNTFLAG	(1U<<16)
#define SYSTICK_RST		0

#define TIM2EN			(1U<<0)
#define CR1_CEN			(1U<<0)
#define DIER_UIE		(1U<<0)

#define INTCTRL				(*((volatile uint32_t *)0xE000ED04))
#define PENDSTSET			(1U<<26)

//uint32_t period_tick;


void osSchedulerBootStrap(void);
void osScheduler(void);

enum thread_state
{
	THREAD_READY = 0,
	THREAD_BLOCKED = 1,
	THREAD_SLEEPING = 2
};

uint32_t 	MILLIS_PRESCALER;

typedef struct tcb
{
	int32_t *stackPt;
	struct tcb *nextPt;

	enum thread_state state; /* New */
	osBinSem *waitSem;        /* New */
	uint32_t wake_at_ms;
} tcbType;

tcbType tcbs[MAX_THREADS];
static int32_t TCB_STACK[MAX_THREADS][STACKSIZE] __attribute__((aligned(8)));
tcbType *currentPt = 0;
volatile uint8_t thread_count = 0;

volatile uint32_t osTimeMs = 0;

/*-----------------------------------------------------------
 * thread_exit
 *
 * Stub function to handle task return.
 * Prevents a task from exiting by looping indefinitely
 * and entering low-power mode (Wait For Interrupt).
 *----------------------------------------------------------*/
static void thread_exit(void)
{
    /* Prevent task from returning */
    while (1)
    {
    	/* Wait for interrupt (low power idle loop) */
    	__WFI();
    }
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
 * osThreadStackInit
 *
 * Initializes the stack for a new thread.
 * Sets up both hardware-saved and software-saved contexts
 * so the thread can start execution properly when scheduled.
 *----------------------------------------------------------*/
void osThreadStackInit(int i, void (*task)(void))
{
	/* Initialize stack pointer (top of stack frame) */
	tcbs[i].stackPt =  &TCB_STACK[i][STACKSIZE - 16]; /*Stack Pointer*/

	/* Hardware-saved frame: [R0,R1,R2,R3,R12,LR,PC,xPSR] */
	TCB_STACK[i][STACKSIZE-1]   = (1U<<24);                /* xPSR: T-bit : Thumb mode */
	TCB_STACK[i][STACKSIZE-2]   = (int32_t)(task);         /* PC */
	TCB_STACK[i][STACKSIZE-3]   = (int32_t)thread_exit;    /*R14 i.e LR*/
	TCB_STACK[i][STACKSIZE-4]   = 0xDEADBEEF;              /*R12*/
	TCB_STACK[i][STACKSIZE-5]   = 0xDEADBEEF;              /*R3*/
	TCB_STACK[i][STACKSIZE-6]   = 0xDEADBEEF;              /*R2*/
	TCB_STACK[i][STACKSIZE-7]   = 0xDEADBEEF;              /*R1*/
	TCB_STACK[i][STACKSIZE-8]   = 0xDEADBEEF;              /*R0*/

	/* Software-saved frame: [R4..R11] */
	TCB_STACK[i][STACKSIZE-9]   = 0xDEADBEEF;              /*R11*/
	TCB_STACK[i][STACKSIZE-10]  = 0xDEADBEEF;              /*R10*/
	TCB_STACK[i][STACKSIZE-11]  = 0xDEADBEEF;              /*R9*/
	TCB_STACK[i][STACKSIZE-12]  = 0xDEADBEEF;              /*R8*/
	TCB_STACK[i][STACKSIZE-13]  = 0xDEADBEEF;              /*R7*/
	TCB_STACK[i][STACKSIZE-14]  = 0xDEADBEEF;              /*R6*/
	TCB_STACK[i][STACKSIZE-15]  = 0xDEADBEEF;              /*R5*/
	TCB_STACK[i][STACKSIZE-16]  = 0xDEADBEEF;              /*R4*/

	/* Set thread state and clear waiting semaphore pointer */
	tcbs[i].state = THREAD_READY;
	tcbs[i].waitSem = 0;
}

/*-----------------------------------------------------------
 * osCreateThread
 *
 * Creates a new thread and links it into the scheduler’s list.
 * Initializes the stack and updates the circular linked list of TCBs.
 * Returns 1 if successful, or 0 if MAX_THREADS is reached.
 *----------------------------------------------------------*/
uint8_t osCreateThread(void(*task)(void))
{
	/* Disable global interrupts to protect critical section */
	__disable_irq();

	/* Check if maximum number of threads has been reached */
	if(thread_count >= MAX_THREADS)
	{
		/* Re-enable interrupts before returning */
		__enable_irq();
		return 0;
	}

	/* Index of new thread */
	int idx = thread_count;

	/* Initialize stack and context for this thread */
	osThreadStackInit(idx, task);

	/* If this is the first thread, point to itself */
	if(thread_count == 0)
	{
		tcbs[0].nextPt = &tcbs[0];
		currentPt = &tcbs[0];
	}
	else
	{
		/* Otherwise, link it circularly after the previous thread */
		tcbs[idx - 1].nextPt = &tcbs[idx];
		tcbs[idx].nextPt = &tcbs[0];
	}

	/* Increment total thread count */
	thread_count++;

	/* Re-enable global interrupts */
	__enable_irq();

	/* Thread created successfully */
	return 1;
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

	/*Set systick to low priority*/
	NVIC_SetPriority(SysTick_IRQn,15);

	/*Enable systick interrupt*/
	//SysTick->CTRL  |= CTRL_TICKINT;
	SysTick->CTRL = CTRL_CLCKSRC | CTRL_TICKINT | CTRL_ENABLE;

	/*Launch scheduler*/
	osSchedulerBootStrap();
}


/*-----------------------------------------------------------
 * SysTick_Handler
 *
 * System tick interrupt handler (context switch mechanism).
 * This handler saves the current thread context, calls the scheduler
 * to pick the next ready thread, restores the new context, and resumes.
 *----------------------------------------------------------*/
/* When exception occurs these registers are automatically
 * saved onto the stack: r0, r1, r2, r3, r12, lr, pc, xPSR.
 */
__attribute__((naked)) void SysTick_Handler(void)
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
 * Requests a context switch by manually triggering the SysTick interrupt.
 * It first clears the current SysTick value register and then sets the
 * PendSV flag through the interrupt control register (INTCTRL).
 *----------------------------------------------------------*/
void osYield(void)
{
	/* Clear SysTick Current Value register to reset the counter */
	SysTick->VAL = 0;

	/* Trigger SysTick interrupt by setting the PENDSTSET bit */
	INTCTRL = PENDSTSET;
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
 * osKernelUnblockOneOnSemaphore
 *
 * Scans the kernel's TCB storage and wakes the first thread
 * blocked on the supplied semaphore. The boolean result lets a
 * synchronization primitive distinguish direct handoff from
 * storing an available signal without inspecting private TCBs.
 *----------------------------------------------------------*/
bool osKernelUnblockOneOnSemaphore(osBinSem *sem)
{
	for(int i = 0; i < thread_count; i++)
	{
		if ((tcbs[i].state == THREAD_BLOCKED) &&
			(tcbs[i].waitSem == sem))
		{
			tcbs[i].state = THREAD_READY;
			tcbs[i].waitSem = 0;
			return true;
		}
	}

	return false;
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
 * osKernelBlockCurrentOnSemaphore
 *
 * Provides the synchronization subsystem with a narrow internal
 * service for blocking the current thread. Keeping TCB access in
 * the kernel prevents semaphore code from depending directly on
 * the scheduler's private data structures.
 *----------------------------------------------------------*/
void osKernelBlockCurrentOnSemaphore(osBinSem *sem)
{
	currentPt->state = THREAD_BLOCKED;
	currentPt->waitSem = sem;
}

/*-----------------------------------------------------------
 * osKernelRequestContextSwitch
 *
 * Provides a portable scheduling request to synchronization
 * primitives. The current Cortex-M implementation delegates to
 * osYield(); a later milestone will route this request to PendSV.
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
    /* Disable interrupts to modify thread state safely */
    __disable_irq();

    /* Calculate absolute wake-up time */
    currentPt->wake_at_ms = osTimeMs + ms;

    /* Mark current thread as sleeping */
    currentPt->state = THREAD_SLEEPING;

    /* Re-enable interrupts to allow other threads/ISRs to proceed */
    __enable_irq();

    /* Yield CPU to switch to another runnable thread */
    osYield();
}
