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
5. Run diagnostic mode 1 below to verify periodic preemption independently
   of worker blocking/yielding.
6. Run diagnostic mode 2 below to verify explicit yield with SysTick stopped.

## Selecting a hardware diagnostic

The application selection is defined in `Inc/osHardwareTest.h`:

| Value | Application | Expected observations |
| --- | --- | --- |
| 0 (default) | Semaphore producer/consumer | Matching counters, LED pulses |
| 1 | Preemption | Both busy-loop worker counters advance without worker yield/delay |
| 2 | Explicit yield | Both worker counters advance with SysTick stopped |

In CubeIDE, open Project Properties > C/C++ Build > Settings > MCU GCC
Compiler > Preprocessor. Add `OS_HARDWARE_TEST_MODE=1` for mode 1 or
`OS_HARDWARE_TEST_MODE=2` for mode 2. Clean, build, and flash after each
change. Alternatively, temporarily edit the default in the selection header
to `OS_HW_TEST_PREEMPTION` or `OS_HW_TEST_YIELD` and restore it after testing.
Do not define the mode in both places. Remove the compiler symbol and
rebuild mode 0 before committing IDE settings or returning to the demo.

Observe UART at 115200 baud without breakpoints for at least 20 reports in
each mode. The startup banner identifies the flashed application. Each
report prints `PASS` or `FAIL`, worker counters A/B, unsigned progress
deltas dA/dB, SysTick control, cumulative failures and the kernel timestamp.
Expect nonzero dA and dB, continuing timestamps, and `failures=0`. Counts do
not need to be equal. A UART stall is also a failed observation: a starved
reporter cannot print its own failure. LED activity alone is insufficient.

Mode 1 workers never yield, delay, or use a semaphore. The reporter also
polls without yielding. SysTick must have ENABLE and TICKINT set in each
sample (other control bits, including COUNTFLAG, may also be set).

Mode 2 uses SysTick only to start the first worker after the initial idle
thread. Worker A then stops SysTick and clears its pending exception before
its loop. Both workers and the reporter explicitly yield. Samples must
show `tick=0x0`. TIM2 continues counting milliseconds and wakes the idle
thread, which explicitly yields after WFI. TIM2 itself does not request a
switch. Thus continued worker progress after setup uses explicit PendSV
requests, not periodic SysTick preemption. This is not a test of switching
with every hardware interrupt disabled.

These checks cover scheduling liveness, not exhaustive preservation of
registers, floating-point context, or invalid blocking-call contexts.

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
