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

	/* Iterate through the bundle to check for duplicates and find first empty slot */
	for (i = 0; i < bundle->size; i++) {
		if (bundle->uthreads[i].present) {
			/* Check for duplicate */
			if (bundle->uthreads[i].uthread == th) {
					log_warn("[LAME]: attempted to add duplicate uthread %p to bundle (kthread %d)",
				 th, myk_index());
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
		 th, myk_index());
		return -ENOSPC;
	}

	/* Add the uthread to the first empty slot */
	bundle->uthreads[first_empty_slot].uthread = th;
	bundle->uthreads[first_empty_slot].present = true;
	bundle->uthreads[first_empty_slot].cycles = 0;
	bundle->uthreads[first_empty_slot].lame_count = 0;
	bundle->used++;
	
	log_debug("[LAME]: added uthread %p to bundle slot %d (kthread %d)",
		 th, first_empty_slot, myk_index());
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

	/* Find the uthread in the bundle */
	for (i = 0; i < bundle->size; i++) {
		if (bundle->uthreads[i].present && bundle->uthreads[i].uthread == th) {
			bundle->uthreads[i].present = false;
			bundle->uthreads[i].uthread = NULL;
			bundle->used--;
			
			log_debug("removed uthread %p from bundle slot %d (kthread %d)",
				 th, i, myk_index());
			return 0;
		}
	}

	return -ENOENT;
}

/**
 * lame_bundle_get_used_count - gets the number of uthreads currently in the bundle
 * @k: the kthread
 *
 * Returns the number of active uthreads.
 */
__always_inline unsigned int lame_bundle_get_used_count(struct kthread *k)
{
	 return k->lame_bundle.used;
}
 
/**
 * lame_sched_get_next_uthread - gets the next uthread to run in round-robin fashion
 * @k: the kthread
 *
 * Returns the next uthread to run, or NULL if none available.
 * The active field in the bundle represents the currently running uthread.
 */
__always_inline thread_t *lame_sched_get_next_uthread(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	unsigned int start_idx, i;

	start_idx = bundle->active;
	
	/* Search for the next present uthread starting from current index */
	for (i = 1; i <= bundle->size; i++) {
		unsigned int idx = (start_idx + i) % bundle->size;
		if (bundle->uthreads[idx].present) {
			bundle->active = idx;
			bundle->total_lames++;
			bundle->uthreads[idx].lame_count++;
			
			log_debug("LAME switch: uthread %p selected from bundle slot %d (kthread %d)",
				 bundle->uthreads[idx].uthread, idx, myk_index());
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
 * The active field in the bundle represents the currently running uthread.
 */
__always_inline thread_t *lame_sched_get_current_uthread(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;

	/* The current uthread is at the active index */
	if (bundle->uthreads[bundle->active].present)
		return bundle->uthreads[bundle->active].uthread;

	return NULL;
}

/**
 * lame_sched_is_enabled - checks if bundle scheduling is enabled
 * @k: the kthread
 *
 * Returns true if bundle scheduling is dynamically enabled,
 * false otherwise.
 */
__always_inline bool lame_sched_is_enabled(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	return bundle->enabled;
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
__always_inline void lame_sched_enable(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	
	bundle->enabled = true;
	log_debug("enabled LAME bundle scheduling for kthread %d",
			myk_index());
}

/**
 * lame_sched_disable - dynamically disables bundle scheduling
 * @k: the kthread
 *
 * This function disables bundle scheduling at runtime. It should be called
 * when entering critical sections where bundle scheduling should be avoided
 * (e.g., during yield operations, scheduler critical sections).
 */
__always_inline void lame_sched_disable(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	
	bundle->enabled = false;
	log_debug("disabled LAME bundle scheduling for kthread %d",
		myk_index());
}

/**
 * lame_sched_is_statically_enabled - checks if bundle scheduling is statically enabled
 * @k: the kthread
 *
 * Returns true if bundle scheduling is statically enabled (bundle size > 1),
 * false otherwise. This is a configuration check, not a runtime state.
 */
__always_inline bool lame_sched_is_statically_enabled(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	return bundle->size > 1;
}

/**
 * lame_sched_is_dynamically_enabled - checks if bundle scheduling is dynamically enabled
 * @k: the kthread
 *
 * Returns true if bundle scheduling is dynamically enabled (enabled flag is true),
 * false otherwise. This should be checked after confirming static enablement
 * with lame_sched_is_statically_enabled().
 */
__always_inline bool lame_sched_is_dynamically_enabled(struct kthread *k)
{
	return k->lame_bundle.enabled;
}

/**
 * lame_bundle_print - prints the bundle array in a neat format
 * @k: the kthread whose bundle to print
 *
 * This function prints the bundle structure in a readable format:
 * - First line shows the bundle metadata fields
 * - Subsequent lines show each uthread wrapper entry
 */
void lame_bundle_print(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	unsigned int i;

	log_info("[LAME][BUNDLE][kthread:%d] size=%u, used=%u, active=%u, total_cycles=%lu, total_lames=%lu, enabled=%d",
		 myk_index(), bundle->size, bundle->used, bundle->active,
		 bundle->total_cycles, bundle->total_lames, bundle->enabled);

	/* Print each uthread wrapper entry */
	for (i = 0; i < bundle->size; i++) {
		struct lame_uthread_wrapper *wrapper = &bundle->uthreads[i];
		
		log_info("[LAME][BUNDLE][kthread:%d][uthread:%p]: slot=%u, present=%d, cycles=%lu, lame_count=%lu",
			 myk_index(), wrapper->uthread, i, wrapper->present, wrapper->cycles, wrapper->lame_count);
	}
}

/**
 * lame_handle - handles LAME exception and performs context switch
 * 
 * This function is called from the assembly __lame_entry after volatile
 * registers are saved. It performs all the LAME handling logic:
 * 1. Get current kthread
 * 2. Check if LAME scheduling is enabled
 * 3. Get current uthread's trapframe
 * 4. Get next uthread from bundle
 * 5. Call __jmp_thread_direct to perform context switch
 */
__always_inline void lame_handle(void)
{
	struct kthread *k = myk();
	thread_t *cur_th, *next_th;

	/* Check if LAME scheduling is enabled */
	if (!lame_sched_is_dynamically_enabled(k)) {
		log_info("[LAME][kthread:%d][func:lame_handle] scheduling disabled",
			myk_index());
		return;
	}

	/* Get current uthread's trapframe */
	cur_th = lame_sched_get_current_uthread(k);
	if (!cur_th) {
		log_info("[LAME][kthread:%d][func:lame_handle] no current uthread",
			myk_index());
		return;
	}

	/* Get next uthread from bundle */
	next_th = lame_sched_get_next_uthread(k);
	if (!next_th) {
		log_info("[LAME][kthread:%d][func:lame_handle] no next uthread available",
			myk_index());
		return;
	}

	log_info("[LAME][kthread:%d][func:lame_handle] switching from uthread %p to %p",
		  myk_index(), cur_th, next_th);

	/* Call __lame_jmp_thread_direct to perform context switch */
	__lame_jmp_thread_direct(&cur_th->tf, &next_th->tf);

	/* This point is reached when switching back to this thread */
	log_info("[LAME][kthread:%d][func:lame_handle] resumed uthread %p",
		  myk_index(), cur_th);
}
/* end */