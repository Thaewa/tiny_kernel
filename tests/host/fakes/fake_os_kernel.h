#ifndef FAKE_OS_KERNEL_H
#define FAKE_OS_KERNEL_H

#include <stdbool.h>
#include <stdint.h>

#include <osSemaphore.h>

/* Resets all recorded internal-kernel calls before each host test. */
void fakeOsKernelReset(void);

/* Configures whether the fake unblock service reports a waiting thread. */
void fakeOsKernelSetUnblockResult(bool result);

uint32_t fakeOsKernelBlockCallCount(void);
uint32_t fakeOsKernelUnblockCallCount(void);
uint32_t fakeOsKernelContextSwitchCallCount(void);
const osBinSem *fakeOsKernelLastBlockedSemaphore(void);
const osBinSem *fakeOsKernelLastUnblockedSemaphore(void);

#endif /* FAKE_OS_KERNEL_H */
