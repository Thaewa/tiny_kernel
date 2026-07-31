#include <osPort.h>

#include "fake_os_port.h"

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
