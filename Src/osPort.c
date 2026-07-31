#include <osPort.h>
#include <cmsis_gcc.h>

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
