# PendSV context-switch verification

SysTick now requests scheduling; PendSV performs the existing MSP-based
integer context switch. TIM2 still supplies the 1 ms kernel clock.
SysTick and PendSV both use priority 15. Other interrupt handlers must not
call blocking task APIs. Floating-point task context is not supported.

## Build and host regression

- Clean and build the Debug configuration in STM32CubeIDE.
- Confirm that the vector table resolves to the new `PendSV_Handler` and
  the ordinary C `SysTick_Handler`, rather than a weak default handler.
- Run the existing binary-semaphore host suite. It verifies portable
  semaphore behavior using fake kernel and port functions; it cannot
  execute Cortex-M exceptions or verify register/stack restoration.

## Hardware checks

1. Run the existing producer/consumer semaphore demo without breakpoints.
   Confirm that UART counters continue increasing and produced equals
   consumed at each reported sample, with the user LED still pulsing.
2. Check that the millisecond timestamp continues increasing and the
   producer delay remains approximately 500 ms plus application overhead.
3. In a separate debug run, break at `SysTick_Handler` and `PendSV_Handler`.
   Confirm that SysTick requests PendSV and that PendSV selects a ready
   task. Inspect `SCB->SHPR` to verify both priorities are lowest.
   Breakpoints alter timing; do not use this run to judge timer intervals.
4. Confirm an unavailable semaphore take switches away and returns only
   after a give wakes its waiter. Confirm the producer's give also reaches
   PendSV when a waiter is readied.
5. To verify periodic preemption independently of blocking/yielding,
   temporarily use two integer-only busy-loop tasks with distinct volatile
   counters. Both counters must advance without calls to `osYield` or
   `osDelay`. Preserve and restore the semaphore demo before committing.
6. To verify explicit yield independently of SysTick, temporarily disable
   SysTick after startup and run two tasks that increment distinct counters
   and call `osYield`. Both counters must advance. Restore the timer and
   demo after this separate diagnostic run.

## Timing and blocking contracts

Explicit yield no longer resets `SysTick->VAL`. Scheduling follows the
original periodic timer phase, so a voluntary switch near a tick may be
followed quickly by another switch. This milestone does not guarantee a
fresh full quantum after every voluntary switch.

PendSV cannot execute while PRIMASK masks configurable exceptions. It also
cannot execute under a BASEPRI mask that excludes its priority. Do not call
a potentially blocking API while the caller masks scheduling. Blocking
context validation and fail-fast handling remain a separate milestone;
moving the handler alone does not enforce this precondition.

## Results

Record the tested commit, toolchain, build result, host-suite result and
hardware observations in the PR. Do not mark exception behavior as verified
based solely on passing host tests or successful compilation.
