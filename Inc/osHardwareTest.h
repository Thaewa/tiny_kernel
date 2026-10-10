#ifndef OS_HARDWARE_TEST_H
#define OS_HARDWARE_TEST_H

/*
 * Select one application at build time. The normal semaphore demo remains
 * the default. To select a diagnostic in STM32CubeIDE, add the symbol
 * OS_HARDWARE_TEST_MODE=1 or OS_HARDWARE_TEST_MODE=2 to the compiler's
 * preprocessor settings, then clean, rebuild, and flash the new ELF.
 * This header also permits a temporary local edit of the default below.
 */
#define OS_HW_TEST_SEMAPHORE  0
#define OS_HW_TEST_PREEMPTION 1
#define OS_HW_TEST_YIELD      2

#ifndef OS_HARDWARE_TEST_MODE
#define OS_HARDWARE_TEST_MODE OS_HW_TEST_SEMAPHORE
//#define OS_HARDWARE_TEST_MODE OS_HW_TEST_PREEMPTION
//#define OS_HARDWARE_TEST_MODE OS_HW_TEST_YIELD
#endif

#if (OS_HARDWARE_TEST_MODE != OS_HW_TEST_SEMAPHORE) && \
    (OS_HARDWARE_TEST_MODE != OS_HW_TEST_PREEMPTION) && \
    (OS_HARDWARE_TEST_MODE != OS_HW_TEST_YIELD)
#error "Unsupported OS_HARDWARE_TEST_MODE: choose 0, 1, or 2"
#endif

#endif /* OS_HARDWARE_TEST_H */
