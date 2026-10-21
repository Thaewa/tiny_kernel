#include <stdio.h>
#include <stdlib.h>
#include "osStack.h"

static unsigned int checks;
#define CHECK(condition) do { \
	checks++; \
	if (!(condition)) { \
		fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #condition); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

static void test_paint_and_accounting(void)
{
	struct { uint32_t before; uint32_t words[32]; uint32_t after; } storage;
	storage.before = 0x11223344U;
	storage.after = 0x55667788U;
	CHECK(osStackInitialize(storage.words, 32U, 4U));
	for (unsigned int i = 0U; i < 32U; i++)
	{
		CHECK(storage.words[i] == (i < 4U ? OS_STACK_GUARD_WORD : OS_STACK_FILL_WORD));
	}
	CHECK(storage.before == 0x11223344U && storage.after == 0x55667788U);
	osStackUsage usage;
	CHECK(osStackMeasure(storage.words, 32U, 4U, &usage));
	CHECK(usage.allocatedWords == 32U && usage.guardWords == 4U);
	CHECK(usage.minimumFreeWords == 28U && usage.maximumUsedWords == 0U);
	CHECK(usage.guardIntact);
	/* One top word, then deeper writes with an untouched gap between. */
	storage.words[31] = 1U;
	CHECK(osStackMeasure(storage.words, 32U, 4U, &usage));
	CHECK(usage.minimumFreeWords == 27U && usage.maximumUsedWords == 1U);
	storage.words[20] = 2U;
	CHECK(osStackMeasure(storage.words, 32U, 4U, &usage));
	CHECK(usage.minimumFreeWords == 16U && usage.maximumUsedWords == 12U);
	/* Simulated unwind does not erase the deepest write. */
	storage.words[31] = 3U;
	CHECK(osStackMeasure(storage.words, 32U, 4U, &usage));
	CHECK(usage.maximumUsedWords == 12U);
	storage.words[4] = 4U;
	CHECK(osStackMeasure(storage.words, 32U, 4U, &usage));
	CHECK(usage.minimumFreeWords == 0U && usage.maximumUsedWords == 28U);
	CHECK(usage.allocatedWords == usage.guardWords + usage.minimumFreeWords + usage.maximumUsedWords);
	CHECK(usage.guardIntact);
	puts("PASS: paint boundaries, deepest-write estimate, gaps and full usage");
}

static void test_every_guard_word(void)
{
	uint32_t storage[24];
	osStackUsage usage;
	for (unsigned int guard = 0U; guard < 8U; guard++)
	{
		CHECK(osStackInitialize(storage, 24U, 8U));
		storage[guard] = 0U;
		CHECK(osStackMeasure(storage, 24U, 8U, &usage));
		CHECK(!usage.guardIntact);
		CHECK(storage[guard] == 0U); /* Inspection does not repair evidence. */
		CHECK(usage.minimumFreeWords == 16U && usage.maximumUsedWords == 0U);
	}
	puts("PASS: damage to each canary word is reported without mutation");
}

static void test_initial_frame_and_pattern_limit(void)
{
	uint32_t storage[400];
	osStackUsage usage;
	CHECK(osStackInitialize(storage, 400U, 8U));
	for (unsigned int i = 384U; i < 400U; i++) storage[i] = 0xDEADBEEFU;
	CHECK(osStackMeasure(storage, 400U, 8U, &usage));
	CHECK(usage.minimumFreeWords == 376U && usage.maximumUsedWords == 16U);
	CHECK(usage.guardIntact);
	/* A write equal to the fill word cannot be distinguished from unused
	 * memory. This intentional test documents the estimator's limitation. */
	storage[100] = OS_STACK_FILL_WORD;
	CHECK(osStackMeasure(storage, 400U, 8U, &usage));
	CHECK(usage.maximumUsedWords == 16U);
	puts("PASS: initial context accounting and documented fill-pattern ambiguity");
}

static void test_invalid_arguments(void)
{
	uint32_t storage[4] = {1U, 2U, 3U, 4U};
	osStackUsage usage = {11U, 12U, 13U, 14U, true};
	CHECK(!osStackInitialize(NULL, 4U, 1U));
	CHECK(!osStackInitialize(storage, 0U, 1U));
	CHECK(!osStackInitialize(storage, 4U, 0U));
	CHECK(!osStackInitialize(storage, 4U, 4U));
	CHECK(!osStackInitialize(storage, 4U, 5U));
	CHECK(storage[0] == 1U && storage[1] == 2U && storage[2] == 3U && storage[3] == 4U);
	CHECK(!osStackMeasure(NULL, 4U, 1U, &usage));
	CHECK(!osStackMeasure(storage, 4U, 1U, NULL));
	CHECK(!osStackMeasure(storage, 0U, 1U, &usage));
	CHECK(!osStackMeasure(storage, 4U, 0U, &usage));
	CHECK(!osStackMeasure(storage, 4U, 4U, &usage));
	CHECK(usage.allocatedWords == 11U && usage.guardWords == 12U);
	CHECK(usage.minimumFreeWords == 13U && usage.maximumUsedWords == 14U && usage.guardIntact);
	puts("PASS: invalid arguments preserve storage and output");
}

int main(void)
{
	test_paint_and_accounting();
	test_every_guard_word();
	test_initial_frame_and_pattern_limit();
	test_invalid_arguments();
	printf("All %u stack diagnostic host checks passed.\n", checks);
	return EXIT_SUCCESS;
}
