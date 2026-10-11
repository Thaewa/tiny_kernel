#include <stdio.h>
#include <stdlib.h>
#include "osWaitQueue.h"

static unsigned int checks;

/* Unlike assert(), these checks also run when NDEBUG is defined. */
#define CHECK(condition) do { \
	checks++; \
	if (!(condition)) { \
		fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

static void checkDetached(const osWaitNode *node, const void *owner)
{
	CHECK(node->container == NULL);
	CHECK(node->next == NULL);
	CHECK(node->previous == NULL);
	CHECK(node->owner == owner);
}

static void test_empty_and_single(void)
{
	osWaitQueue queue;
	osWaitNode node;
	osWaitNode *out = &node;
	int owner = 17;
	CHECK(osWaitQueueInit(&queue) == OS_WAIT_OK);
	CHECK(osWaitNodeInit(&node, &owner) == OS_WAIT_OK);
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_OK);
	CHECK(osWaitQueuePop(&queue, &out) == OS_WAIT_EMPTY);
	CHECK(out == NULL);
	CHECK(queue.count == 0U);
	CHECK(osWaitQueuePush(&queue, &node) == OS_WAIT_OK);
	CHECK(queue.head == &node && queue.tail == &node && queue.count == 1U);
	CHECK(node.container == &queue);
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_OK);
	CHECK(osWaitQueuePop(&queue, &out) == OS_WAIT_OK);
	CHECK(out == &node);
	checkDetached(&node, &owner);
	CHECK(queue.head == NULL && queue.tail == NULL && queue.count == 0U);
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_OK);
	puts("PASS: empty and single-member queue");
}

static void test_fifo_and_reuse(void)
{
	osWaitQueue queue;
	osWaitNode nodes[3];
	osWaitNode *out;
	CHECK(osWaitQueueInit(&queue) == OS_WAIT_OK);
	for (unsigned int i = 0U; i < 3U; i++)
	{
		CHECK(osWaitNodeInit(&nodes[i], &nodes[i]) == OS_WAIT_OK);
		CHECK(osWaitQueuePush(&queue, &nodes[i]) == OS_WAIT_OK);
	}
	/* Cycling through all nodes proves FIFO ordering and repeatable reuse. */
	for (unsigned int turn = 0U; turn < 1000U; turn++)
	{
		CHECK(osWaitQueuePop(&queue, &out) == OS_WAIT_OK);
		CHECK(out == &nodes[turn % 3U]);
		checkDetached(out, out);
		CHECK(osWaitQueuePush(&queue, out) == OS_WAIT_OK);
		CHECK(queue.count == 3U);
		CHECK(osWaitQueueValidate(&queue) == OS_WAIT_OK);
	}
	puts("PASS: FIFO order across 1000 pop/reinsert operations");
}

static void test_duplicate_and_cross_queue(void)
{
	osWaitQueue first, second;
	osWaitNode a, b;
	CHECK(osWaitQueueInit(&first) == OS_WAIT_OK);
	CHECK(osWaitQueueInit(&second) == OS_WAIT_OK);
	CHECK(osWaitNodeInit(&a, NULL) == OS_WAIT_OK);
	CHECK(osWaitNodeInit(&b, NULL) == OS_WAIT_OK);
	CHECK(osWaitQueuePush(&first, &a) == OS_WAIT_OK);
	/* Duplicate singleton insertion is the classic self-link trigger. */
	CHECK(osWaitQueuePush(&first, &a) == OS_WAIT_ALREADY_LINKED);
	CHECK(a.next == NULL && a.previous == NULL);
	CHECK(osWaitQueuePush(&first, &b) == OS_WAIT_OK);
	CHECK(osWaitQueuePush(&first, &a) == OS_WAIT_ALREADY_LINKED);
	CHECK(osWaitQueuePush(&first, &b) == OS_WAIT_ALREADY_LINKED);
	CHECK(osWaitQueuePush(&second, &a) == OS_WAIT_ALREADY_LINKED);
	CHECK(first.head == &a && first.tail == &b && first.count == 2U);
	CHECK(a.next == &b && b.previous == &a);
	CHECK(a.container == &first && b.container == &first);
	CHECK(second.head == NULL && second.tail == NULL && second.count == 0U);
	CHECK(osWaitQueueValidate(&first) == OS_WAIT_OK);
	CHECK(osWaitQueueValidate(&second) == OS_WAIT_OK);
	puts("PASS: duplicate and cross-queue insertion leave lists unchanged");
}

static void test_remove_members(void)
{
	osWaitQueue queue, other;
	osWaitNode nodes[3], outsider;
	CHECK(osWaitQueueInit(&queue) == OS_WAIT_OK);
	CHECK(osWaitQueueInit(&other) == OS_WAIT_OK);
	CHECK(osWaitNodeInit(&outsider, NULL) == OS_WAIT_OK);
	for (unsigned int i = 0U; i < 3U; i++)
	{
		CHECK(osWaitNodeInit(&nodes[i], &nodes[i]) == OS_WAIT_OK);
		CHECK(osWaitQueuePush(&queue, &nodes[i]) == OS_WAIT_OK);
	}
	CHECK(osWaitQueueRemove(&queue, &outsider) == OS_WAIT_NOT_MEMBER);
	CHECK(osWaitQueueRemove(&other, &nodes[0]) == OS_WAIT_NOT_MEMBER);
	CHECK(queue.count == 3U && other.count == 0U);
	/* Middle, tail, then singleton cover both neighbor and endpoint updates. */
	CHECK(osWaitQueueRemove(&queue, &nodes[1]) == OS_WAIT_OK);
	CHECK(nodes[0].next == &nodes[2] && nodes[2].previous == &nodes[0]);
	checkDetached(&nodes[1], &nodes[1]);
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_OK);
	CHECK(osWaitQueueRemove(&queue, &nodes[2]) == OS_WAIT_OK);
	CHECK(queue.head == &nodes[0] && queue.tail == &nodes[0]);
	CHECK(osWaitQueueRemove(&queue, &nodes[0]) == OS_WAIT_OK);
	CHECK(queue.count == 0U && osWaitQueueValidate(&queue) == OS_WAIT_OK);
	CHECK(osWaitQueueRemove(&queue, &nodes[0]) == OS_WAIT_NOT_MEMBER);
	/* A removed node can subsequently enter a different queue. */
	CHECK(osWaitQueuePush(&other, &nodes[1]) == OS_WAIT_OK);
	CHECK(nodes[1].container == &other);
	puts("PASS: arbitrary removal and transfer after unlink");
}

static void test_capacity(void)
{
	osWaitQueue queue;
	osWaitNode nodes[OS_WAIT_QUEUE_MAX_NODES + 1U];
	osWaitNode *out;
	CHECK(osWaitQueueInit(&queue) == OS_WAIT_OK);
	for (unsigned int i = 0U; i <= OS_WAIT_QUEUE_MAX_NODES; i++)
	{
		CHECK(osWaitNodeInit(&nodes[i], NULL) == OS_WAIT_OK);
	}
	for (unsigned int i = 0U; i < OS_WAIT_QUEUE_MAX_NODES; i++)
	{
		CHECK(osWaitQueuePush(&queue, &nodes[i]) == OS_WAIT_OK);
	}
	CHECK(osWaitQueuePush(&queue, &nodes[OS_WAIT_QUEUE_MAX_NODES]) == OS_WAIT_FULL);
	CHECK(queue.count == OS_WAIT_QUEUE_MAX_NODES);
	checkDetached(&nodes[OS_WAIT_QUEUE_MAX_NODES], NULL);
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_OK);
	CHECK(osWaitQueuePop(&queue, &out) == OS_WAIT_OK && out == &nodes[0]);
	CHECK(osWaitQueuePush(&queue, &nodes[OS_WAIT_QUEUE_MAX_NODES]) == OS_WAIT_OK);
	CHECK(queue.tail == &nodes[OS_WAIT_QUEUE_MAX_NODES]);
	puts("PASS: bounded capacity and reuse after full queue");
}

static void test_corruption_detection(void)
{
	osWaitQueue queue;
	osWaitNode a, b, spare;
	osWaitNode *out = &spare;
	CHECK(osWaitQueueInit(&queue) == OS_WAIT_OK);
	CHECK(osWaitNodeInit(&a, NULL) == OS_WAIT_OK);
	CHECK(osWaitNodeInit(&b, NULL) == OS_WAIT_OK);
	CHECK(osWaitNodeInit(&spare, NULL) == OS_WAIT_OK);
	CHECK(osWaitQueuePush(&queue, &a) == OS_WAIT_OK);
	CHECK(osWaitQueuePush(&queue, &b) == OS_WAIT_OK);

	/* Inject accessible logical corruption; operations must not repair it
	 * silently or mutate other nodes. Reset one injected field at a time. */
	a.next = &a;
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_CORRUPT);
	CHECK(osWaitQueuePush(&queue, &spare) == OS_WAIT_CORRUPT);
	CHECK(osWaitQueuePop(&queue, &out) == OS_WAIT_CORRUPT && out == NULL);
	CHECK(osWaitQueueRemove(&queue, &b) == OS_WAIT_CORRUPT);
	CHECK(a.next == &a && b.previous == &a && queue.count == 2U);
	checkDetached(&spare, NULL);
	a.next = &b;
	b.previous = NULL;
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_CORRUPT);
	b.previous = &a;
	b.container = NULL;
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_CORRUPT);
	b.container = &queue;
	queue.count = UINT32_MAX;
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_CORRUPT);
	queue.count = 1U;
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_CORRUPT);
	queue.count = 3U;
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_CORRUPT);
	queue.count = 2U;
	queue.tail = &a;
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_CORRUPT);
	queue.tail = &b;
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_OK);

	/* Forging container alone must never allow removal of an outsider. */
	spare.container = &queue;
	CHECK(osWaitQueueRemove(&queue, &spare) == OS_WAIT_CORRUPT);
	CHECK(queue.head == &a && queue.tail == &b && queue.count == 2U);
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_OK);
	puts("PASS: self-links, mismatched topology/count, and forged membership");
}

static void test_arguments_and_stale_links(void)
{
	osWaitQueue queue;
	osWaitNode node, spare;
	osWaitNode *out = &node;
	CHECK(osWaitQueueInit(&queue) == OS_WAIT_OK);
	CHECK(osWaitNodeInit(&node, NULL) == OS_WAIT_OK);
	CHECK(osWaitNodeInit(&spare, NULL) == OS_WAIT_OK);
	CHECK(osWaitQueueInit(NULL) == OS_WAIT_INVALID_ARGUMENT);
	CHECK(osWaitNodeInit(NULL, NULL) == OS_WAIT_INVALID_ARGUMENT);
	CHECK(osWaitQueueValidate(NULL) == OS_WAIT_INVALID_ARGUMENT);
	CHECK(osWaitQueuePush(NULL, &node) == OS_WAIT_INVALID_ARGUMENT);
	CHECK(osWaitQueuePush(&queue, NULL) == OS_WAIT_INVALID_ARGUMENT);
	CHECK(osWaitQueuePop(NULL, &out) == OS_WAIT_INVALID_ARGUMENT && out == NULL);
	CHECK(osWaitQueuePop(&queue, NULL) == OS_WAIT_INVALID_ARGUMENT);
	CHECK(osWaitQueueRemove(NULL, &node) == OS_WAIT_INVALID_ARGUMENT);
	CHECK(osWaitQueueRemove(&queue, NULL) == OS_WAIT_INVALID_ARGUMENT);
	node.next = &spare;
	CHECK(osWaitQueuePush(&queue, &node) == OS_WAIT_CORRUPT);
	CHECK(queue.count == 0U && node.next == &spare);
	node.next = NULL;
	node.previous = &spare;
	CHECK(osWaitQueuePush(&queue, &node) == OS_WAIT_CORRUPT);
	CHECK(queue.count == 0U);
	puts("PASS: null arguments and stale detached-node links");
}

static void test_cycle_and_empty_topology(void)
{
	osWaitQueue queue;
	osWaitNode a, b, c;
	CHECK(osWaitQueueInit(&queue) == OS_WAIT_OK);
	CHECK(osWaitNodeInit(&a, NULL) == OS_WAIT_OK);
	CHECK(osWaitNodeInit(&b, NULL) == OS_WAIT_OK);
	CHECK(osWaitNodeInit(&c, NULL) == OS_WAIT_OK);
	/* Even count zero must agree with both endpoints. */
	queue.head = &a;
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_CORRUPT);
	queue.head = NULL;
	queue.tail = &a;
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_CORRUPT);
	queue.tail = NULL;
	CHECK(osWaitQueuePush(&queue, &a) == OS_WAIT_OK);
	CHECK(osWaitQueuePush(&queue, &b) == OS_WAIT_OK);
	CHECK(osWaitQueuePush(&queue, &c) == OS_WAIT_OK);
	/* A -> B -> A is a multi-node cycle with an unreachable but otherwise
	 * valid tail C. It must not hang the validation walk. */
	b.next = &a;
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_CORRUPT);
	CHECK(b.next == &a && queue.count == 3U);
	b.next = &c;
	queue.head = NULL;
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_CORRUPT);
	queue.head = &a;
	queue.tail = NULL;
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_CORRUPT);
	queue.tail = &c;
	CHECK(osWaitQueueValidate(&queue) == OS_WAIT_OK);
	puts("PASS: multi-node cycle and inconsistent empty/endpoints");
}

int main(void)
{
	test_empty_and_single();
	test_fifo_and_reuse();
	test_duplicate_and_cross_queue();
	test_remove_members();
	test_capacity();
	test_corruption_detection();
	test_arguments_and_stale_links();
	test_cycle_and_empty_topology();
	printf("All %u wait queue host checks passed.\n", checks);
	return EXIT_SUCCESS;
}
