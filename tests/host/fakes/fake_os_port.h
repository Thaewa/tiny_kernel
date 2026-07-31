#ifndef FAKE_OS_PORT_H
#define FAKE_OS_PORT_H

#include <stdint.h>

#define FAKE_OS_PORT_SAVED_IRQ_STATE (0xA5A5A5A5U)

/* Resets all recorded port calls before each host test. */
void fakeOsPortReset(void);

uint32_t fakeOsPortEnterCriticalCallCount(void);
uint32_t fakeOsPortExitCriticalCallCount(void);
uint32_t fakeOsPortMemoryBarrierCallCount(void);
uint32_t fakeOsPortLastRestoredIrqState(void);

#endif /* FAKE_OS_PORT_H */
