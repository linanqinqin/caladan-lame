/*
 * test_lame_bundle.c - test program for LAME bundle scheduling data structures
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <base/log.h>
#include <base/init.h>
#include <runtime/runtime.h>
#include <runtime/thread.h>

#include "defs.h"

static int test_bundle_initialization(void)
{
	struct kthread *k = myk();
	int ret;

	printf("Testing bundle initialization...\n");

	/* Test bundle initialization */
	ret = lame_bundle_init(k);
	if (ret) {
		printf("FAILED: lame_bundle_init returned %d\n", ret);
		return -1;
	}

	/* Verify bundle state */
	if (!k->lame_bundle.uthreads) {
		printf("FAILED: uthreads array not allocated\n");
		return -1;
	}

	if (k->lame_bundle.size != cfg_lame_bundle_size) {
		printf("FAILED: bundle size mismatch, expected %d, got %d\n",
		       cfg_lame_bundle_size, k->lame_bundle.size);
		return -1;
	}

		if (k->lame_bundle.used != 0) {
		printf("FAILED: used should be 0, got %d\n",
			k->lame_bundle.used);
		return -1;
	}

	if (cfg_lame_bundle_size > 1 && !k->lame_bundle.enabled) {
		printf("FAILED: bundle should be enabled for size > 1\n");
		return -1;
	}

	printf("PASSED: bundle initialization\n");
	return 0;
}

static int test_bundle_uthread_management(void)
{
	struct kthread *k = myk();
	thread_t *test_threads[LAME_BUNDLE_SIZE_MAX];
	int i, ret;

	printf("Testing uthread management...\n");

	/* Create some test threads */
	for (i = 0; i < k->lame_bundle.size; i++) {
		test_threads[i] = malloc(sizeof(thread_t));
		if (!test_threads[i]) {
			printf("FAILED: could not allocate test thread %d\n", i);
			return -1;
		}
		memset(test_threads[i], 0, sizeof(thread_t));
	}

	/* Test adding uthreads */
	for (i = 0; i < k->lame_bundle.size; i++) {
		ret = lame_bundle_add_uthread(k, test_threads[i]);
		if (ret) {
			printf("FAILED: lame_bundle_add_uthread returned %d for thread %d\n", ret, i);
			return -1;
		}
	}

	/* Verify all threads were added */
		if (k->lame_bundle.used != k->lame_bundle.size) {
		printf("FAILED: used should be %d, got %d\n",
			k->lame_bundle.size, k->lame_bundle.used);
		return -1;
	}

	/* Test adding one more (should fail) */
	ret = lame_bundle_add_uthread(k, test_threads[0]);
	if (ret != -ENOSPC) {
		printf("FAILED: should have failed with -ENOSPC, got %d\n", ret);
		return -1;
	}

	/* Test removing uthreads */
	for (i = 0; i < k->lame_bundle.size; i++) {
		ret = lame_bundle_remove_uthread(k, test_threads[i]);
		if (ret) {
			printf("FAILED: lame_bundle_remove_uthread returned %d for thread %d\n", ret, i);
			return -1;
		}
	}

	/* Verify all threads were removed */
	if (k->lame_bundle.used != 0) {
		printf("FAILED: used should be 0, got %d\n", k->lame_bundle.used);
		return -1;
	}

	/* Test removing non-existent thread */
	ret = lame_bundle_remove_uthread(k, test_threads[0]);
	if (ret != -ENOENT) {
		printf("FAILED: should have failed with -ENOENT, got %d\n", ret);
		return -1;
	}

	/* Clean up test threads */
	for (i = 0; i < k->lame_bundle.size; i++) {
		free(test_threads[i]);
	}

	printf("PASSED: uthread management\n");
	return 0;
}

static int test_bundle_round_robin(void)
{
	struct kthread *k = myk();
	thread_t *test_threads[LAME_BUNDLE_SIZE_MAX];
	thread_t *selected;
	int i, ret;

	printf("Testing round-robin scheduling...\n");

	if (k->lame_bundle.size < 2) {
		printf("SKIPPED: bundle size < 2, cannot test round-robin\n");
		return 0;
	}

	/* Create test threads */
	for (i = 0; i < k->lame_bundle.size; i++) {
		test_threads[i] = malloc(sizeof(thread_t));
		if (!test_threads[i]) {
			printf("FAILED: could not allocate test thread %d\n", i);
			return -1;
		}
		memset(test_threads[i], 0, sizeof(thread_t));
	}

	/* Add threads to bundle */
	for (i = 0; i < k->lame_bundle.size; i++) {
		ret = lame_bundle_add_uthread(k, test_threads[i]);
		if (ret) {
			printf("FAILED: lame_bundle_add_uthread returned %d for thread %d\n", ret, i);
			return -1;
		}
	}

	/* Test round-robin selection */
	for (i = 0; i < k->lame_bundle.size * 2; i++) {
		selected = lame_bundle_get_next_uthread(k);
		if (!selected) {
			printf("FAILED: lame_bundle_get_next_uthread returned NULL\n");
			return -1;
		}

		/* Verify it's one of our test threads */
		int found = 0;
		for (int j = 0; j < k->lame_bundle.size; j++) {
			if (selected == test_threads[j]) {
				found = 1;
				break;
			}
		}
		if (!found) {
			printf("FAILED: selected thread not in bundle\n");
			return -1;
		}
	}

	/* Clean up test threads */
	for (i = 0; i < k->lame_bundle.size; i++) {
		free(test_threads[i]);
	}

	printf("PASSED: round-robin scheduling\n");
	return 0;
}

static int test_bundle_cleanup(void)
{
	struct kthread *k = myk();

	printf("Testing bundle cleanup...\n");

	lame_bundle_cleanup(k);

	/* Verify cleanup */
	if (k->lame_bundle.uthreads) {
		printf("FAILED: uthreads array not freed\n");
		return -1;
	}

	if (k->lame_bundle.size != 0) {
		printf("FAILED: size should be 0, got %d\n", k->lame_bundle.size);
		return -1;
	}

	if (k->lame_bundle.used != 0) {
		printf("FAILED: used should be 0, got %d\n", k->lame_bundle.used);
		return -1;
	}

	if (k->lame_bundle.enabled) {
		printf("FAILED: bundle should be disabled after cleanup\n");
		return -1;
	}

	printf("PASSED: bundle cleanup\n");
	return 0;
}

static void main_handler(void *arg)
{
	int ret = 0;

	printf("=== LAME Bundle Data Structure Test ===\n");
	printf("Bundle size: %d\n", cfg_lame_bundle_size);

	ret |= test_bundle_initialization();
	ret |= test_bundle_uthread_management();
	ret |= test_bundle_round_robin();
	ret |= test_bundle_cleanup();

	if (ret == 0) {
		printf("=== ALL TESTS PASSED ===\n");
	} else {
		printf("=== SOME TESTS FAILED ===\n");
	}

	runtime_exit();
}

int main(int argc, char *argv[])
{
	int ret;

	ret = runtime_init(argv[1], main_handler, NULL);
	if (ret) {
		printf("failed to start runtime\n");
		return ret;
	}

	return 0;
}
