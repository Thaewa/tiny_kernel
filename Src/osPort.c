#include <osPort.h>
#include <cmsis_gcc.h>

volatile osBlockingFaultRecord osLastBlockingFault;

osExecutionContext osPortGetExecutionContext(void)
{
	osExecutionContext caller = {
		__get_IPSR(), __get_PRIMASK(), __get_BASEPRI(), __get_FAULTMASK()
	};
	return caller;
}

/*
 * A void take/delay API cannot report rejection as an ordinary return.
 * Save the original caller snapshot, mask interrupts, and stop at a named
 * location for the debugger. Do not printf here: UART may require interrupts
 * or locks. This remains active under NDEBUG and does not silently unmask
 * a caller's critical section. Reset is required to resume normal operation.
 */
_Noreturn void osPortBlockingFault(uint32_t errors, uint32_t operation,
                                 osExecutionContext caller)
{
	__disable_irq();
	osLastBlockingFault.errors = errors;
	osLastBlockingFault.operation = operation;
	osLastBlockingFault.caller = caller;
	__DSB();
	while (1)
	{
		__NOP();
	}
}

/*-----------------------------------------------------------
 * osPortEnterCritical
 *
 * Saves the Cortex-M PRIMASK value before disabling maskable
 * interrupts. Saving the previous value allows nested kernel
 * critical sections to restore the caller's original state
 * instead of enabling interrupts unconditionally.
 *----------------------------------------------------------*/
osIrqState osPortEnterCritical(void)
{
	osIrqState previousState = __get_PRIMASK();

	__disable_irq();
	return previousState;
}

/*-----------------------------------------------------------
 * osPortExitCritical
 *
 * Restores the exact PRIMASK value captured when the critical
 * section was entered. Interrupts remain disabled when the
 * caller had already disabled them before entering the kernel.
 *----------------------------------------------------------*/
void osPortExitCritical(osIrqState state)
{
	__set_PRIMASK(state);
}

/*-----------------------------------------------------------
 * osPortMemoryBarrier
 *
 * Ensures that shared kernel-state updates complete before
 * another thread or exception observes that state.
 *----------------------------------------------------------*/
void osPortMemoryBarrier(void)
{
	__DMB();
}
