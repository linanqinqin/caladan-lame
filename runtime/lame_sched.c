/* linanqinqin */
/*
 * lame_sched.c - LAME bundle scheduling support
 */

#include <stdlib.h>
#include <string.h>

#include <base/log.h>
#include <base/mem.h>

#include "defs.h"

/* External configuration variable */
extern unsigned int cfg_lame_bundle_size;

/**
 * lame_bundle_init - initializes a LAME bundle for a kthread
 * @k: the kthread to initialize the bundle for
 *
 * Returns 0 if successful, or -ENOMEM if out of memory.
 */
int lame_bundle_init(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	size_t array_size;

	/* Allocate the uthread wrapper array */
	array_size = cfg_lame_bundle_size * sizeof(struct lame_uthread_wrapper);
	bundle->uthreads = mem_alloc(array_size);
	if (!bundle->uthreads) {
		log_err("failed to allocate uthread wrapper array for bundle");
		return -ENOMEM;
	}

	/* Initialize the bundle structure */
	memset(bundle->uthreads, 0, array_size);
	bundle->size = cfg_lame_bundle_size;
	bundle->active = 0;
	bundle->used = 0;
	bundle->total_cycles = 0;
	bundle->total_lames = 0;
	bundle->enabled = (cfg_lame_bundle_size > 1);

	log_info("initialized LAME bundle for kthread %d with size %d",
		 kthread_idx(k), bundle->size);

	return 0;
}

/**
 * lame_bundle_cleanup - cleans up a LAME bundle
 * @k: the kthread whose bundle to clean up
 */
void lame_bundle_cleanup(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;

	if (bundle->uthreads) {
		mem_free(bundle->uthreads);
		bundle->uthreads = NULL;
	}

	bundle->size = 0;
	bundle->active = 0;
	bundle->used = 0;
	bundle->total_cycles = 0;
	bundle->total_lames = 0;
	bundle->enabled = false;
}

/**
 * lame_bundle_add_uthread - adds a uthread to the bundle
 * @k: the kthread
 * @th: the uthread to add
 *
 * Returns 0 if successful, or -ENOSPC if bundle is full.
 */
int lame_bundle_add_uthread(struct kthread *k, thread_t *th)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	unsigned int i;

	if (!bundle->enabled || !bundle->uthreads)
		return -EINVAL;

	/* Find an empty slot */
	for (i = 0; i < bundle->size; i++) {
		if (!bundle->uthreads[i].present) {
			bundle->uthreads[i].uthread = th;
			bundle->uthreads[i].present = true;
			bundle->uthreads[i].cycles = 0;
			bundle->uthreads[i].lame_count = 0;
			bundle->used++;
			
			log_debug("added uthread %p to bundle slot %d (kthread %d)",
				 th, i, kthread_idx(k));
			return 0;
		}
	}

	return -ENOSPC;
}

/**
 * lame_bundle_remove_uthread - removes a uthread from the bundle
 * @k: the kthread
 * @th: the uthread to remove
 *
 * Returns 0 if successful, or -ENOENT if uthread not found.
 */
int lame_bundle_remove_uthread(struct kthread *k, thread_t *th)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	unsigned int i;

	if (!bundle->enabled || !bundle->uthreads)
		return -EINVAL;

	/* Find the uthread in the bundle */
	for (i = 0; i < bundle->size; i++) {
		if (bundle->uthreads[i].present && bundle->uthreads[i].uthread == th) {
			bundle->uthreads[i].present = false;
			bundle->uthreads[i].uthread = NULL;
			bundle->used--;
			
			log_debug("removed uthread %p from bundle slot %d (kthread %d)",
				 th, i, kthread_idx(k));
			return 0;
		}
	}

	return -ENOENT;
}

/**
 * lame_bundle_get_next_uthread - gets the next uthread to run in round-robin fashion
 * @k: the kthread
 *
 * Returns the next uthread to run, or NULL if no uthreads are available.
 */
thread_t *lame_bundle_get_next_uthread(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	unsigned int start_idx, i;

	if (!bundle->enabled || !bundle->uthreads || bundle->used == 0)
		return NULL;

	start_idx = bundle->active;
	
	/* Search for the next present uthread starting from current index */
	for (i = 0; i < bundle->size; i++) {
		unsigned int idx = (start_idx + i) % bundle->size;
		if (bundle->uthreads[idx].present) {
			bundle->active = (idx + 1) % bundle->size;
			bundle->total_lames++;
			bundle->uthreads[idx].lame_count++;
			
			log_debug("LAME switch: uthread %p selected from bundle slot %d (kthread %d)",
				 bundle->uthreads[idx].uthread, idx, kthread_idx(k));
			return bundle->uthreads[idx].uthread;
		}
	}

	return NULL;
}

/**
 * lame_bundle_get_current_uthread - gets the currently active uthread
 * @k: the kthread
 *
 * Returns the currently active uthread, or NULL if none.
 */
thread_t *lame_bundle_get_current_uthread(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	unsigned int prev_idx;

	if (!bundle->enabled || !bundle->uthreads || bundle->used == 0)
		return NULL;

	/* The current uthread is the one before the active */
	prev_idx = (bundle->active + bundle->size - 1) % bundle->size;
	
	if (bundle->uthreads[prev_idx].present)
		return bundle->uthreads[prev_idx].uthread;

	return NULL;
}

/**
 * lame_bundle_is_enabled - checks if bundle scheduling is enabled
 * @k: the kthread
 *
 * Returns true if bundle scheduling is enabled, false otherwise.
 */
bool lame_bundle_is_enabled(struct kthread *k)
{
	return k->lame_bundle.enabled;
}

/**
 * lame_bundle_get_used_count - gets the number of active uthreads in the bundle
 * @k: the kthread
 *
 * Returns the number of active uthreads.
 */
unsigned int lame_bundle_get_used_count(struct kthread *k)
{
	return k->lame_bundle.used;
}
/* end */