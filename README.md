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
- On-demand stack usage estimates and sampled canaries
- Configurable scheduling time quantum
- Millisecond task delay
- Thread states: ready, blocked, and sleeping
- Idle thread using `WFI`
- Binary semaphore
- Counting semaphore with bounded capacity and FIFO handoff
- Non-recursive mutex with owner-only release and FIFO ownership handoff
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

The [basic mutex](docs/basic-mutex.md) tracks its owning task, rejects
wrong-owner release and detects self-Take. Priority inheritance is not
implemented; hardware verification uses diagnostic mode 5.

[Stack diagnostics](docs/stack-diagnostics.md) report painted high-water
usage and low-end canary state. Allocation remains fixed at 400 words per
thread; diagnostic mode 6 compares shallow and deeper workloads. Canary
checks are sampled, not automatic overflow protection.

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
├── tiny_kernel.launch.example  Shared ST-LINK debug template
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
5. Create a local `tiny_kernel.launch` from `tiny_kernel.launch.example`
   using the command below, then refresh the project in CubeIDE.
6. Use the local launch to program and debug with ST-LINK. Verify that
   **C/C++ Application** is `Debug/tiny_kernel.elf` in Debug Configurations.

Run this once from the repository root in PowerShell. It preserves an
existing local launch instead of overwriting your debugger settings:

```powershell
if (-not (Test-Path -LiteralPath 'tiny_kernel.launch')) {
    Copy-Item -LiteralPath 'tiny_kernel.launch.example' -Destination 'tiny_kernel.launch'
}
```

### Local IDE settings

Root-level `.launch` files, `.settings/language.settings.xml` and SFR view
state are local files excluded from version control. CubeIDE may update
them freely without creating Git changes. Existing local settings are kept
when removing these files from tracking; fresh clones copy the launch
template and let CubeIDE regenerate compiler discovery settings.

The template keeps the shared ELF, project, SWD and ST-LINK configuration.
Logging is disabled and no machine-specific log path is included. If you
enable server logging, choose a local log destination in CubeIDE. To share
an intentional debugger configuration change, edit the `.launch.example`
template rather than committing a generated local `.launch` file.

`.project`, `.cproject` and shared editor preferences remain tracked because
they define the project/build configuration. Do not ignore all of `.settings/`.

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
bool osGetThreadStackUsage(uint8_t threadIndex, osStackUsage *result);
void osKernelLaunch(uint32_t quanta);
void osYield(void);
void osDelay(uint32_t ms);

void osBinSemInit(osBinSem *sem, int32_t initial);
void osBinSemTake(osBinSem *sem);
void osBinSemGive(osBinSem *sem);

bool osCountSemInit(osCountSem *sem, uint32_t maxCount, uint32_t initialCount);
void osCountSemTake(osCountSem *sem);
bool osCountSemGive(osCountSem *sem);

void osMutexInit(osMutex *mutex);
void osMutexTake(osMutex *mutex);
bool osMutexGive(osMutex *mutex);
```

## Roadmap

The next planned milestones are:

- Extend kernel API contracts and error handling
- Add caller-provided stacks and configurable per-thread sizes
- Add automatic stack checks and improve portability
- Add priority-based scheduling
- Add priority inheritance for mutexes
- Add message queues and event flags

## Limitations

- Synchronization edge cases have not yet been fully tested
- No task priorities or priority inheritance
- No recursive mutex, mutex timeout or general deadlock detection
- No semaphore timeout
- No dynamic task creation after the kernel starts has been validated
- No automatic stack overflow trapping or memory protection
- Stack painting estimates writes, not a guaranteed worst-case stack bound
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
