#include <osPort.h>

#include "fake_os_port.h"
#include <stdlib.h>

volatile osBlockingFaultRecord osLastBlockingFault;
jmp_buf fakeOsPortFaultJump;
int fakeOsPortFaultExpected;
static osExecutionContext executionContext;

void fakeOsPortSetExecutionContext(osExecutionContext caller)
{
	executionContext = caller;
}

osExecutionContext osPortGetExecutionContext(void)
{
	return executionContext;
}

_Noreturn void osPortBlockingFault(uint32_t errors, uint32_t operation,
                                 osExecutionContext caller)
{
	osLastBlockingFault.errors = errors;
	osLastBlockingFault.operation = operation;
	osLastBlockingFault.caller = caller;
	if (!fakeOsPortFaultExpected)
	{
		abort();
	}
	longjmp(fakeOsPortFaultJump, 1);
}

static uint32_t enterCriticalCallCount;
static uint32_t exitCriticalCallCount;
static uint32_t memoryBarrierCallCount;
static osIrqState lastRestoredIrqState;

void fakeOsPortReset(void)
{
	enterCriticalCallCount = 0U;
	exitCriticalCallCount = 0U;
	memoryBarrierCallCount = 0U;
	lastRestoredIrqState = 0U;
	executionContext = (osExecutionContext){0U, 0U, 0U, 0U};
	osLastBlockingFault = (osBlockingFaultRecord){0};
	fakeOsPortFaultExpected = 0;
}

osIrqState osPortEnterCritical(void)
{
	enterCriticalCallCount++;
	return FAKE_OS_PORT_SAVED_IRQ_STATE;
}

void osPortExitCritical(osIrqState state)
{
	exitCriticalCallCount++;
	lastRestoredIrqState = state;
}

void osPortMemoryBarrier(void)
{
	memoryBarrierCallCount++;
}

uint32_t fakeOsPortEnterCriticalCallCount(void)
{
	return enterCriticalCallCount;
}

uint32_t fakeOsPortExitCriticalCallCount(void)
{
	return exitCriticalCallCount;
}

uint32_t fakeOsPortMemoryBarrierCallCount(void)
{
	return memoryBarrierCallCount;
}

uint32_t fakeOsPortLastRestoredIrqState(void)
{
	return lastRestoredIrqState;
}
