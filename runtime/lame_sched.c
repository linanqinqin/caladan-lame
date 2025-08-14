/* linanqinqin */
/*
 * lame_sched.c - LAME bundle scheduling support
 */

#include <stdlib.h>
#include <string.h>

#include <base/log.h>

#include "defs.h"

/* External configuration variable */
extern unsigned int cfg_lame_bundle_size;

/**
 * lame_bundle_init - initializes a LAME bundle for a kthread
 * @k: the kthread to initialize the bundle for
 *
 * Returns 0 if successful, or -ENOMEM if out of memory.
 */
void lame_bundle_init(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;

	/* Initialize the bundle structure */
	memset(bundle->uthreads, 0, sizeof(bundle->uthreads));
	bundle->size = cfg_lame_bundle_size;
	bundle->active = 0;
	bundle->used = 0;
	bundle->total_cycles = 0;
	bundle->total_lames = 0;
	bundle->enabled = false; /* Start disabled */

}

/**
 * lame_bundle_cleanup - cleans up a LAME bundle
 * @k: the kthread whose bundle to clean up
 */
void lame_bundle_cleanup(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;

	/* Reset the bundle structure */
	memset(bundle->uthreads, 0, sizeof(bundle->uthreads));
	bundle->size = 0;
	bundle->active = 0;
	bundle->used = 0;
	bundle->total_cycles = 0;
	bundle->total_lames = 0;
	bundle->enabled = false; /* Disable when cleaning up */
}

/**
 * lame_bundle_add_uthread - adds a uthread to the bundle
 * @k: the kthread
 * @th: the uthread to add
 *
 * Returns 0 if successful, or -ENOSPC if bundle is full, or -EINVAL if invalid.
 */
int lame_bundle_add_uthread(struct kthread *k, thread_t *th)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	unsigned int i;
	int first_empty_slot = -1;

	/* Check if bundle scheduling is statically enabled (size > 1) */
	if (bundle->size <= 1)
		return -EINVAL;

	/* Iterate through the bundle to check for duplicates and find first empty slot */
	for (i = 0; i < bundle->size; i++) {
		if (bundle->uthreads[i].present) {
			/* Check for duplicate */
			if (bundle->uthreads[i].uthread == th) {
				log_warn("[LAME]: attempted to add duplicate uthread %p to bundle (kthread %d)",
					 th, kthread_idx(k));
				return 0; /* Return gracefully, no error */
			}
		} else {
			/* Record first empty slot */
			if (first_empty_slot == -1) {
				first_empty_slot = i;
			}
		}
	}

	/* Check if we found an empty slot */
	if (first_empty_slot == -1) {
		log_debug("[LAME]: bundle is full, cannot add uthread %p (kthread %d)",
			 th, kthread_idx(k));
		return -ENOSPC;
	}

	/* Add the uthread to the first empty slot */
	bundle->uthreads[first_empty_slot].uthread = th;
	bundle->uthreads[first_empty_slot].present = true;
	bundle->uthreads[first_empty_slot].cycles = 0;
	bundle->uthreads[first_empty_slot].lame_count = 0;
	bundle->used++;
	
	log_debug("[LAME]: added uthread %p to bundle slot %d (kthread %d)",
		 th, first_empty_slot, kthread_idx(k));
	return 0;
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

	/* Check if bundle scheduling is statically enabled (size > 1) */
	if (bundle->size <= 1)
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
 * lame_bundle_get_used_count - gets the number of active uthreads in the bundle
 * @k: the kthread
 *
 * Returns the number of active uthreads.
 */
 unsigned int lame_bundle_get_used_count(struct kthread *k)
 {
	 return k->lame_bundle.used;
 }
 
/**
 * lame_sched_get_next_uthread - gets the next uthread to run in round-robin fashion
 * @k: the kthread
 *
 * Returns the next uthread to run, or NULL if no uthreads are available.
 */
thread_t *lame_sched_get_next_uthread(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	unsigned int start_idx, i;

	/* Check if bundle scheduling is statically enabled (size > 1) */
	if (bundle->size <= 1)
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
 * lame_sched_get_current_uthread - gets the currently active uthread
 * @k: the kthread
 *
 * Returns the currently active uthread, or NULL if none.
 */
thread_t *lame_sched_get_current_uthread(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	unsigned int prev_idx;

	/* Check if bundle scheduling is statically enabled (size > 1) */
	if (bundle->size <= 1)
		return NULL;

	/* The current uthread is the one before the active */
	prev_idx = (bundle->active + bundle->size - 1) % bundle->size;
	
	if (bundle->uthreads[prev_idx].present)
		return bundle->uthreads[prev_idx].uthread;

	return NULL;
}

/**
 * lame_sched_is_enabled - checks if bundle scheduling is enabled
 * @k: the kthread
 *
 * Returns true if bundle scheduling is both statically and dynamically enabled,
 * false otherwise. Static enablement requires size > 1, dynamic enablement
 * requires the enabled flag to be true.
 */
bool lame_sched_is_enabled(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	return (bundle->size > 1) && bundle->enabled;
}

/**
 * lame_sched_enable - dynamically enables bundle scheduling
 * @k: the kthread
 *
 * This function enables bundle scheduling at runtime. It should be called
 * when entering safe sections where bundle scheduling is allowed.
 * Note: Bundle scheduling must be statically enabled (size > 1) for this
 * to have any effect.
 */
void lame_sched_enable(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	
	if (bundle->size > 1) {
		bundle->enabled = true;
		log_debug("enabled LAME bundle scheduling for kthread %d",
			 kthread_idx(k));
	}
}

/**
 * lame_sched_disable - dynamically disables bundle scheduling
 * @k: the kthread
 *
 * This function disables bundle scheduling at runtime. It should be called
 * when entering critical sections where bundle scheduling should be avoided
 * (e.g., during yield operations, scheduler critical sections).
 */
void lame_sched_disable(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	
	if (bundle->size > 1) {
		bundle->enabled = false;
		log_debug("disabled LAME bundle scheduling for kthread %d",
			 kthread_idx(k));
	}
}

/**
 * lame_sched_is_statically_enabled - checks if bundle scheduling is statically enabled
 * @k: the kthread
 *
 * Returns true if bundle size > 1, indicating bundle scheduling is configured
 * and available for use.
 */
bool lame_sched_is_statically_enabled(struct kthread *k)
{
	return k->lame_bundle.size > 1;
}

/**
 * lame_sched_is_dynamically_enabled - checks if bundle scheduling is dynamically enabled
 * @k: the kthread
 *
 * Returns true if the dynamic enabled flag is set. This should only be checked
 * after confirming static enablement with lame_sched_is_statically_enabled().
 */
bool lame_sched_is_dynamically_enabled(struct kthread *k)
{
	return k->lame_bundle.enabled;
}
/* end */