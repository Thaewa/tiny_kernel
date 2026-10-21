#ifndef OS_THREAD_H
#define OS_THREAD_H

#include <stdint.h>
#include "osStack.h"

#define OS_THREAD_DEFAULT_STACK_WORDS 400U
#define OS_THREAD_MIN_STACK_WORDS 64U
#define OS_THREAD_STACK_GUARD_WORDS 8U

/* Startup only, before osKernelLaunch; return 1 on success, 0 on rejection.
 * Neither return value is a task handle. Both calls preserve IRQ masking.
 * The legacy API uses a kernel-owned 400-word buffer. */
uint8_t osCreateThread(void (*task)(void));

/* Supply writable RAM with an 8-byte-aligned base and an EVEN word count.
 * Words are uint32_t, not bytes, and include the guard and initial context.
 * The minimum is only an admission floor, NOT a safe size for every task.
 * Storage must remain exclusively assigned for the task's entire lifetime;
 * use static/global storage, never an automatic startup local. No heap is
 * used and no memory is freed. Rejection does not paint or publish a task.
 * Arbitrary RAM accessibility/lifetime cannot be verified by this API. */
uint8_t osCreateThreadStatic(void (*task)(void), uint32_t *stack,
                             uint32_t stackWords);

/* Diagnostic slot index: idle=0, then creation order; NOT a task handle.
 * Task/startup context only. Invalid calls leave result unchanged. A true
 * return is a valid measurement: inspect guardIntact separately. */
bool osGetThreadStackUsage(uint8_t threadIndex, osStackUsage *result);

#endif /* OS_THREAD_H */
