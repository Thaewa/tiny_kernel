#include <osKernelInternal.h>

#include "fake_os_kernel.h"

static uint32_t blockCallCount;
static uint32_t contextSwitchCallCount;
static osBinSem *lastBlockedSemaphore;

void fakeOsKernelReset(void)
{
	blockCallCount = 0U;
	contextSwitchCallCount = 0U;
	lastBlockedSemaphore = 0;
}

void osKernelBlockCurrentOnSemaphore(osBinSem *sem)
{
	blockCallCount++;
	lastBlockedSemaphore = sem;
}

void osKernelRequestContextSwitch(void)
{
	contextSwitchCallCount++;
}

uint32_t fakeOsKernelBlockCallCount(void)
{
	return blockCallCount;
}

uint32_t fakeOsKernelContextSwitchCallCount(void)
{
	return contextSwitchCallCount;
}

const osBinSem *fakeOsKernelLastBlockedSemaphore(void)
{
	return lastBlockedSemaphore;
}
