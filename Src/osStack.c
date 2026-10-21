#include "osStack.h"
#include <stddef.h>

bool osStackInitialize(uint32_t *storage, uint32_t words, uint32_t guardWords)
{
	if (storage == NULL || guardWords == 0U || guardWords >= words) return false;
	for (uint32_t i = 0U; i < words; i++)
	{
		storage[i] = (i < guardWords) ? OS_STACK_GUARD_WORD : OS_STACK_FILL_WORD;
	}
	return true;
}

/* Scan the untouched low-address prefix, stopping at its first changed word.
 * The total usable region includes context frames and all software stack
 * writes. Rewinding SP does not repaint old writes, so normal stack unwind
 * retains the historical footprint. Guard words are reported separately.
 * Work is O(words), so the kernel exposes this on demand rather than doing
 * a full scan on every SysTick/PendSV switch. */
bool osStackMeasure(const uint32_t *storage, uint32_t words,
                    uint32_t guardWords, osStackUsage *result)
{
	if (storage == NULL || result == NULL || guardWords == 0U || guardWords >= words)
	{
		return false;
	}
	bool intact = true;
	for (uint32_t i = 0U; i < guardWords; i++)
	{
		if (storage[i] != OS_STACK_GUARD_WORD) intact = false;
	}
	uint32_t freeWords = 0U;
	for (uint32_t i = guardWords; i < words; i++)
	{
		if (storage[i] != OS_STACK_FILL_WORD) break;
		freeWords++;
	}
	result->allocatedWords = words;
	result->guardWords = guardWords;
	result->minimumFreeWords = freeWords;
	result->maximumUsedWords = words - guardWords - freeWords;
	result->guardIntact = intact;
	return true;
}
