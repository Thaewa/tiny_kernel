#ifndef OS_STACK_H
#define OS_STACK_H

#include <stdbool.h>
#include <stdint.h>

#define OS_STACK_FILL_WORD  0xA5A5A5A5U
#define OS_STACK_GUARD_WORD 0xDEADC0DEU

typedef struct
{
	uint32_t allocatedWords;
	uint32_t guardWords;
	uint32_t minimumFreeWords;
	uint32_t maximumUsedWords;
	bool guardIntact;
} osStackUsage;

/* Downward-growing stack: guard occupies the lowest addresses. Initialize
 * fresh inactive storage only, before installing the initial context frame.
 * One word is uint32_t (4 bytes on the STM32 target). */
bool osStackInitialize(uint32_t *storage, uint32_t words, uint32_t guardWords);

/* Inspect without changing storage. Caller supplies accessible initialized
 * storage of the stated length and serializes concurrent writers.
 * Returns false for invalid arguments, leaving result unchanged. A true
 * return means a measurement was obtained, NOT that the guard is intact.
 * Painted usage is an estimate of the deepest write, not a live SP value
 * or protection against memory corruption. See docs/stack-diagnostics.md. */
bool osStackMeasure(const uint32_t *storage, uint32_t words,
                    uint32_t guardWords, osStackUsage *result);

#endif /* OS_STACK_H */
