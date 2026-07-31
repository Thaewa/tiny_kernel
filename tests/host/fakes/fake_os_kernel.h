#ifndef FAKE_OS_KERNEL_H
#define FAKE_OS_KERNEL_H

#include <stdint.h>

#include <osSemaphore.h>

/* Resets all recorded internal-kernel calls before each host test. */
void fakeOsKernelReset(void);

uint32_t fakeOsKernelBlockCallCount(void);
uint32_t fakeOsKernelContextSwitchCallCount(void);
const osBinSem *fakeOsKernelLastBlockedSemaphore(void);

#endif /* FAKE_OS_KERNEL_H */
