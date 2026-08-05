#include <osKernelInternal.h>

#include "fake_os_kernel.h"

static uint32_t blockCallCount;
static uint32_t unblockCallCount;
static uint32_t contextSwitchCallCount;
static osBinSem *lastBlockedSemaphore;
static osBinSem *lastUnblockedSemaphore;
static bool unblockResult;

void fakeOsKernelReset(void)
{
	blockCallCount = 0U;
	unblockCallCount = 0U;
	contextSwitchCallCount = 0U;
	lastBlockedSemaphore = 0;
	lastUnblockedSemaphore = 0;
	unblockResult = false;
}

void fakeOsKernelSetUnblockResult(bool result)
{
	unblockResult = result;
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

bool osKernelUnblockOneOnSemaphore(osBinSem *sem)
{
	unblockCallCount++;
	lastUnblockedSemaphore = sem;
	return unblockResult;
}

uint32_t fakeOsKernelBlockCallCount(void)
{
	return blockCallCount;
}

uint32_t fakeOsKernelContextSwitchCallCount(void)
{
	return contextSwitchCallCount;
}

uint32_t fakeOsKernelUnblockCallCount(void)
{
	return unblockCallCount;
}

const osBinSem *fakeOsKernelLastBlockedSemaphore(void)
{
	return lastBlockedSemaphore;
}

const osBinSem *fakeOsKernelLastUnblockedSemaphore(void)
{
	return lastUnblockedSemaphore;
}
