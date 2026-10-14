# Tiny Kernel

Tiny Kernel is a small educational real-time kernel for ARM Cortex-M
microcontrollers. It is being developed from scratch to explore task
scheduling, context switching, synchronization, and other RTOS fundamentals.

The current target is the STM32F411RE (ARM Cortex-M4F). This project is a
learning project and is not yet intended for production or safety-critical
systems.

## Current Features

- Preemptive round-robin scheduling
- Up to eight threads
- Separate stack for each thread
- Configurable scheduling time quantum
- Millisecond task delay
- Thread states: ready, blocked, and sleeping
- Idle thread using `WFI`
- Binary semaphore
- Counting semaphore with bounded capacity and FIFO handoff
- Basic critical sections

## Current Status

The kernel can create and schedule multiple threads and wake sleeping threads
using a 1 ms timer tick. A binary semaphore implementation is present, but its
behavior under multiple waiters, interrupt usage, and edge cases still needs
systematic testing.

Each binary semaphore owns a [FIFO wait queue](docs/fifo-wait-queue.md).
Give selects the oldest waiter rather than scanning TCB creation order.
See [semaphore FIFO integration](docs/semaphore-fifo-integration.md) for
host coverage and the multi-waiter hardware diagnostic.

The [counting semaphore](docs/counting-semaphore.md) shares that wait
machinery and reports Give overflow when its stored count is full.

## Target Hardware

- MCU: STM32F411RE
- CPU: ARM Cortex-M4F
- Development board: STM32 Nucleo-F411RE or a compatible STM32F411RE target
- System clock assumed by the kernel: 16 MHz
- Debug probe: ST-LINK

## Development Environment

- STM32CubeIDE
- GNU Tools for STM32
- CMSIS device and core headers

The current project was generated with GNU Tools for STM32 `13.3.rel1`.
Using a different toolchain version may produce a different ELF file even when
the generated firmware behaves identically.

## Project Structure

```text
Tiny_Kernel/
├── Inc/            Public headers
├── Src/            Kernel, application, GPIO, and UART sources
├── Startup/        STM32 startup code
├── chip_headers/   CMSIS core and STM32 device headers
├── .settings/      STM32CubeIDE project settings
├── .cproject       Eclipse CDT build configuration
├── .project        Eclipse project definition
├── tiny_kernel.launch
├── STM32F411RETX_FLASH.ld
└── STM32F411RETX_RAM.ld
```

`Debug/` contains generated build output and is intentionally excluded from
version control.

## Building

1. Open STM32CubeIDE.
2. Import the repository as an existing project.
3. Select the `Debug` configuration.
4. Build the `tiny_kernel` project.
5. Use `tiny_kernel.launch` to program and debug the target with ST-LINK.

The expected debug executable is:

```text
Debug/tiny_kernel.elf
```

## Kernel Overview

Each thread has a task control block and a statically allocated stack. Threads
are linked in a circular list and the scheduler selects the next thread in the
ready state.

SysTick requests periodic preemption and PendSV performs context switching.
`osYield()` also requests PendSV without resetting the SysTick counter. TIM2
provides the independent 1 ms kernel time base used by `osDelay()`.

See [PendSV verification](docs/pendsv-context-switch-test-plan.md) for the
hardware checks required to verify exception handling and stack restoration.

The public kernel API currently includes:

```c
void osInit(void);
uint8_t osCreateThread(void (*task)(void));
void osKernelLaunch(uint32_t quanta);
void osYield(void);
void osDelay(uint32_t ms);

void osBinSemInit(osBinSem *sem, int32_t initial);
void osBinSemTake(osBinSem *sem);
void osBinSemGive(osBinSem *sem);

bool osCountSemInit(osCountSem *sem, uint32_t maxCount, uint32_t initialCount);
void osCountSemTake(osCountSem *sem);
bool osCountSemGive(osCountSem *sem);
```

## Roadmap

The next planned milestones are:

- Add repeatable tests for binary semaphore behavior
- Define kernel API contracts and error handling
- Introduce reusable wait queues and blocking primitives
- Separate synchronization code from the kernel core
- Add mutex ownership and misuse detection
- Add priority-based scheduling
- Add priority inheritance for mutexes
- Add message queues and event flags
- Improve stack diagnostics and portability

## Limitations

- Synchronization edge cases have not yet been fully tested
- No task priorities or priority inheritance
- No semaphore timeout
- No dynamic task creation after the kernel starts has been validated
- No stack overflow detection
- No floating-point task context preservation
- Blocking calls require scheduling to be unmasked; invalid take/delay calls
  stop with debugger diagnostics (see [contract](docs/blocking-context-contract.md))
- Timing configuration currently assumes a 16 MHz system clock
- Some generated STM32CubeIDE build files may contain machine-specific paths

## Project Goals

The goals of Tiny Kernel are to:

- Learn how an RTOS works below its public API
- Keep kernel behavior small enough to inspect and reason about
- Build synchronization and scheduling features incrementally
- Maintain clear milestone baselines for regression comparison
- Evolve toward a reusable embedded kernel without hiding important details

## License

Original Tiny Kernel source code is licensed under the MIT License.
See [LICENSE](LICENSE) for details.

Third-party CMSIS and STM32 device files remain subject to their respective
licenses and copyright notices.
