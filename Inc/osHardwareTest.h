#ifndef OS_HARDWARE_TEST_H
#define OS_HARDWARE_TEST_H

/*
 * Select one application at build time. The normal semaphore demo remains
 * the default. To select a diagnostic in STM32CubeIDE, add the symbol
 * OS_HARDWARE_TEST_MODE=1, 2, 3, or 4 to the compiler's
 * preprocessor settings, then clean, rebuild, and flash the new ELF.
 * This header also permits a temporary local edit of the default below.
 */
#define OS_HW_TEST_SEMAPHORE  0
#define OS_HW_TEST_PREEMPTION 1
#define OS_HW_TEST_YIELD      2
#define OS_HW_TEST_SEMAPHORE_FIFO 3
#define OS_HW_TEST_COUNTING_SEMAPHORE 4

/* Mode 3 verifies C,A,B FIFO wake selection with actual blocking tasks. */

#ifndef OS_HARDWARE_TEST_MODE
#define OS_HARDWARE_TEST_MODE OS_HW_TEST_SEMAPHORE
//#define OS_HARDWARE_TEST_MODE OS_HW_TEST_PREEMPTION
//#define OS_HARDWARE_TEST_MODE OS_HW_TEST_YIELD
//#define OS_HARDWARE_TEST_MODE OS_HW_TEST_SEMAPHORE_FIFO
//#define OS_HARDWARE_TEST_MODE OS_HW_TEST_COUNTING_SEMAPHORE
#endif

#if (OS_HARDWARE_TEST_MODE != OS_HW_TEST_SEMAPHORE) && \
    (OS_HARDWARE_TEST_MODE != OS_HW_TEST_PREEMPTION) && \
    (OS_HARDWARE_TEST_MODE != OS_HW_TEST_YIELD) && \
    (OS_HARDWARE_TEST_MODE != OS_HW_TEST_SEMAPHORE_FIFO) && \
    (OS_HARDWARE_TEST_MODE != OS_HW_TEST_COUNTING_SEMAPHORE)
#error "Unsupported OS_HARDWARE_TEST_MODE: choose 0, 1, 2, 3, or 4"
#endif

#endif /* OS_HARDWARE_TEST_H */
