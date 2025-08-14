/*
 * lame.h - LAME bundle scheduling public interface
 */

#pragma once

#include <base/thread.h>

/* Forward declarations */
struct kthread;

/* LAME bundle management functions */
extern int lame_bundle_add_uthread(struct kthread *k, thread_t *th);
extern int lame_bundle_remove_uthread(struct kthread *k, thread_t *th);
extern unsigned int lame_bundle_get_used_count(struct kthread *k);

/* LAME scheduling functions */
extern thread_t *lame_sched_get_next_uthread(struct kthread *k);
extern thread_t *lame_sched_get_current_uthread(struct kthread *k);
extern bool lame_sched_is_enabled(struct kthread *k);

/* Dynamic bundle scheduling control functions */
extern void lame_sched_enable(struct kthread *k);
extern void lame_sched_disable(struct kthread *k);
extern bool lame_sched_is_statically_enabled(struct kthread *k);
extern bool lame_sched_is_dynamically_enabled(struct kthread *k);

/* Internal symbols for testing (not part of public API) */
#ifdef LAME_TESTING
extern unsigned int cfg_lame_bundle_size;
#endif
