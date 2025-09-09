/* linanqinqin */
/*
 * lame_sched.c - LAME bundle scheduling support
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
 * @set_active: if true, set this uthread as the active one in the bundle
 *
 * Returns 0 if successful, or -ENOSPC if bundle is full, or -EINVAL if invalid.
 */
int lame_bundle_add_uthread(struct kthread *k, thread_t *th, bool set_active)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	unsigned int i;
	int first_empty_slot = -1;

	log_debug("[LAME][kthread:%d][func:lame_bundle_add_uthread] adding uthread %p (set_active=%d)",
			myk_index(), th, set_active);

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
	
	/* If this is the uthread that will run next, update the active index */
	if (set_active) {
		bundle->active = first_empty_slot;
		log_debug("[LAME][kthread:%d][func:lame_bundle_add_uthread] set active index to %d for uthread %p",
				myk_index(), first_empty_slot, th);
	}
	
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
	
			log_debug("[LAME][kthread:%d][uthread:%p][func:lame_bundle_remove_uthread] removing uthread at index %u",
					myk_index(), th, i);

			bundle->uthreads[i].present = false;
			bundle->uthreads[i].uthread = NULL;
			bundle->used--;
			
			return 0;
		}
	}

	log_debug("[LAME][kthread:%d][uthread:%p][func:lame_bundle_remove_uthread] uthread not found",
			myk_index(), th);
	return -ENOENT;
}

/**
 * lame_bundle_remove_uthread_by_index - removes a uthread from the bundle by index
 * @k: the kthread
 * @index: the index of the uthread to remove
 *
 * Returns 0 if successful, or -EINVAL if index is out of bounds, or -ENOENT if uthread not present.
 */
int lame_bundle_remove_uthread_by_index(struct kthread *k, unsigned int index)
{
	struct lame_bundle *bundle = &k->lame_bundle;

	log_debug("[LAME][kthread:%d][func:lame_bundle_remove_uthread_by_index] removing uthread at index %u",
			myk_index(), index);

	if (index >= bundle->size) {
		log_err("[LAME][kthread:%d][func:lame_bundle_remove_uthread_by_index] index %u out of bounds",
			myk_index(), index);
		return -EINVAL;
	}

	if (bundle->uthreads[index].present) {
		bundle->uthreads[index].present = false;
		bundle->uthreads[index].uthread = NULL;
		bundle->used--;
		return 0;
	}
	else {
		return -ENOENT;
	}
}

/**
 * lame_bundle_remove_uthread_at_active - removes the uthread at the active index
 * @k: the kthread
 *
 * Returns 0 if successful, or -ENOENT if uthread not present.
 */
int lame_bundle_remove_uthread_at_active(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	unsigned int active = bundle->active;
	
	if (bundle->uthreads[active].present) {
	
		log_debug("[LAME][kthread:%d][uthread:%p][func:lame_bundle_remove_uthread_at_active] removing uthread at active index %u",
			myk_index(), bundle->uthreads[active].uthread, active);

		bundle->uthreads[active].present = false;
		bundle->uthreads[active].uthread = NULL;
		bundle->used--;
		return 0;
	}
	else {
		log_debug("[LAME][kthread:%d][func:lame_bundle_remove_uthread_at_active] uthread at active index %u not present",
			myk_index(), active);
		return -ENOENT;
	}
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
			
			log_debug("[LAME][kthread:%d][func:lame_sched_get_next_uthread] uthread %p selected from bundle slot %d",
				 myk_index(), bundle->uthreads[idx].uthread, idx);
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
	log_debug("[LAME][kthread:%d][func:lame_sched_enable] enabled LAME bundle scheduling",
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
	log_debug("[LAME][kthread:%d][func:lame_sched_disable] disabled LAME bundle scheduling",
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
	char log_buf[512];
	int offset;

	/* linanqinqin */
	offset = snprintf(log_buf, sizeof(log_buf), 
			"[LAME][BUNDLE][kthread:%d][size:%u][used:%u][active:%u][enabled:%d][bundle:", 
			myk_index(), bundle->size, bundle->used, bundle->active, bundle->enabled);

	/* Print each uthread wrapper entry in format: <uthread0><uthread1>...<uthreadn-1> */
	for (i = 0; i < bundle->size && offset < (int)sizeof(log_buf) - 1; i++) {
		struct lame_uthread_wrapper *wrapper = &bundle->uthreads[i];
		offset += snprintf(log_buf + offset, sizeof(log_buf) - offset, 
				  "<%p>", wrapper->uthread);
	}

	/* Close the bundle bracket */
	offset += snprintf(log_buf + offset, sizeof(log_buf) - offset, "]");
	/* end */

	log_info("%s", log_buf); 
}

/**
 * lame_bundle_set_ready_false_all - sets thread_ready to false for all uthreads in the bundle
 * @k: the kthread
 *
 * This function sets thread_ready = false for all uthreads in the bundle.
 * This is called when uthreads are added to the bundle to maintain the illusion
 * that they are "running" from Caladan's perspective.
 */
void lame_bundle_set_ready_false_all(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	unsigned int i;

	for (i = 0; i < bundle->size; i++) {
		if (bundle->uthreads[i].present) {
			bundle->uthreads[i].uthread->thread_ready = false;
		}
	}

	log_debug("[LAME][kthread:%d][func:lame_bundle_set_ready_false_all]",
		 myk_index());
}

/**
 * lame_bundle_set_running_true_all - sets thread_running to true for all uthreads in the bundle
 * @k: the kthread
 *
 * This function sets thread_running = true for all uthreads in the bundle.
 * This is called when uthreads are added to the bundle to maintain the illusion
 * that they are "running" from Caladan's perspective.
 */
void lame_bundle_set_running_true_all(struct kthread *k)
{
	struct lame_bundle *bundle = &k->lame_bundle;
	unsigned int i;

	for (i = 0; i < bundle->size; i++) {
		if (bundle->uthreads[i].present) {
			bundle->uthreads[i].uthread->thread_running = true;
		}
	}

	log_debug("[LAME][kthread:%d][func:lame_bundle_set_running_true_all]",
		 myk_index());
}

__always_inline static void __lame_bundle_to_rq(struct kthread *k) 
{
	struct lame_bundle *bundle = &k->lame_bundle;
	uint64_t now_tsc = rdtsc();

	/* Iterate through all bundle slots */
	for (unsigned int i = 0; i < bundle->size; i++) {
		if (bundle->uthreads[i].present) {
			thread_t *th = bundle->uthreads[i].uthread;
			uint32_t rq_tail;
			
			/* Mark uthread as ready */
			th->thread_ready = true;
			th->thread_running = false;
			th->ready_tsc = now_tsc;
			
			/* Add uthread back to runqueue (similar to thread_ready but without accounting) */
			rq_tail = load_acquire(&k->rq_tail);
			if (unlikely(k->rq_head - rq_tail >= RUNTIME_RQ_SIZE ||
			             !list_empty_volatile(&k->rq_overflow))) {
				/* Runqueue is full, add to overflow list */
				assert(k->rq_head - rq_tail <= RUNTIME_RQ_SIZE);
				list_add_tail(&k->rq_overflow, &th->link);
				drain_overflow(k);
				ACCESS_ONCE(k->q_ptrs->rq_head)++;
			} else {
				/* Add to main runqueue */
				k->rq[k->rq_head % RUNTIME_RQ_SIZE] = th;
				store_release(&k->rq_head, k->rq_head + 1);
				if (k->rq_head - load_acquire(&k->rq_tail) == 1)
					ACCESS_ONCE(k->q_ptrs->oldest_tsc) = th->ready_tsc;
				ACCESS_ONCE(k->q_ptrs->rq_head)++;
			}
			
			/* Clear the bundle slot */
			bundle->uthreads[i].present = false;
			bundle->uthreads[i].uthread = NULL;
			bundle->uthreads[i].cycles = 0;
			bundle->uthreads[i].lame_count = 0;
			
			log_debug("[LAME][kthread:%d][func:lame_sched_bundle_dismantle] returned uthread %p to runqueue",
				 myk_index(), th);
		}
	}
}

/**
 * lame_sched_bundle_dismantle - dismantles the bundle and returns all uthreads to runqueue
 * @k: the kthread
 *
 * This function is called when a uthread is descheduled to dismantle the entire bundle.
 * It removes all uthreads from the bundle and adds them back to Caladan's runqueue.
 * This ensures bundle lifecycle is tied to Caladan's scheduling lifecycle.
 *
 * The function does not perform accounting or statistics as those are handled by
 * Caladan's regular descheduling procedure.
 */
void lame_sched_bundle_dismantle(struct kthread *k)
{
	if (k->lame_bundle.used >= 1) {
		spin_lock(&k->lock);
		__lame_bundle_to_rq(k);
		spin_unlock(&k->lock);
	}

	/* Reset bundle state */
	k->lame_bundle.used = 0;
	k->lame_bundle.active = 0;
}

/**
 * lame_sched_bundle_dismantle - dismantles the bundle and returns all uthreads to runqueue
 * @k: the kthread
 *
 * This function is identical to lame_sched_bundle_dismantle, but assumes the lock is already held.
 */
void lame_sched_bundle_dismantle_nolock(struct kthread *k)
{
	assert_spin_lock_held(&k->lock);

	if (k->lame_bundle.used >= 1) {
		__lame_bundle_to_rq(k);
	}

	/* Reset bundle state */
	k->lame_bundle.used = 0;
	k->lame_bundle.active = 0;
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
		/* this should not happen */
		log_err("[LAME][kthread:%d][func:lame_handle] scheduling disabled",
			myk_index());
		preempt_enable();
		return;
	}

	/* If there is only one uthread in the bundle, no need to schedule */
	if (lame_bundle_get_used_count(k) <= 1) {
		preempt_enable();
		return;
	}

	/* Get current uthread's trapframe */
	cur_th = lame_sched_get_current_uthread(k);
	if (!cur_th) {
		log_err("[LAME][kthread:%d][func:lame_handle] no current uthread",
			myk_index());
		preempt_enable();
		return;
	}

	/* Get next uthread from bundle */
	next_th = lame_sched_get_next_uthread(k);
	if (!next_th) {
		log_err("[LAME][kthread:%d][func:lame_handle] no next uthread available",
			myk_index());
		preempt_enable();
		return;
	}

	log_debug("[LAME][kthread:%d][func:lame_handle] switching from uthread %p to %p",
		  myk_index(), cur_th, next_th);

	/* Update __self to point to the new uthread */
	perthread_store(__self, next_th);

	/* Call __lame_jmp_thread_direct to perform context switch */
	__lame_jmp_thread_direct(&cur_th->tf, &next_th->tf);

	/* This point is reached when switching back to this thread */
	log_debug("[LAME][kthread:%d][func:lame_handle] resumed uthread %p",
		  myk_index(), cur_th);
}

#ifdef CONFIG_LAME_TSC
void lame_print_tsc_counters(void)
{
	unsigned int i;
	for (i = 0; i < maxks; i++) {
		struct kthread *k = ks[i];
		if (!k)
			continue;
		log_warn("[LAME][TSC][kthread:%u] avg_cycles=%lu total_cycles=%lu total_lames=%lu", i,
				 k->lame_bundle.total_lames? k->lame_bundle.total_cycles / k->lame_bundle.total_lames : 0, 
				 k->lame_bundle.total_cycles, k->lame_bundle.total_lames);
	}
}
#endif
/* end */