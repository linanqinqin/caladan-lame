/*
 * init.c - initializes the runtime
 */

#include <pthread.h>

#include <base/cpu.h>
#include <base/init.h>
#include <base/log.h>
#include <base/limits.h>
#include <runtime/thread.h>

/* linanqinqin */
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/lame.h>
/* External configuration variable */
extern unsigned int cfg_lame_bundle_size;
extern unsigned int cfg_lame_tsc;
extern unsigned int cfg_lame_register;
/* end */

#include "defs.h"

static pthread_barrier_t init_barrier;

struct init_entry {
	const char *name;
	int (*init)(void);
};

static initializer_fn_t global_init_hook = NULL;
static initializer_fn_t perthread_init_hook = NULL;
static initializer_fn_t late_init_hook = NULL;


#define GLOBAL_INITIALIZER(name) \
	{__cstr(name), &name ## _init}

/* global subsystem initialization */
static const struct init_entry global_init_handlers[] = {
	/* runtime core */
	GLOBAL_INITIALIZER(kthread),
	GLOBAL_INITIALIZER(ioqueues),
	GLOBAL_INITIALIZER(runtime_stack),
	GLOBAL_INITIALIZER(sched),
	GLOBAL_INITIALIZER(preempt),
	GLOBAL_INITIALIZER(smalloc),

	/* network stack */
	GLOBAL_INITIALIZER(net),
	GLOBAL_INITIALIZER(udp),
	GLOBAL_INITIALIZER(directpath),
	GLOBAL_INITIALIZER(arp),
	GLOBAL_INITIALIZER(trans),

	/* storage */
	GLOBAL_INITIALIZER(storage),

#ifdef GC
	GLOBAL_INITIALIZER(gc),
#endif
};

#define THREAD_INITIALIZER(name) \
	{__cstr(name), &name ## _init_thread}

/* per-kthread subsystem initialization */
static const struct init_entry thread_init_handlers[] = {
	/* runtime core */
	THREAD_INITIALIZER(preempt),
	THREAD_INITIALIZER(kthread),
	THREAD_INITIALIZER(ioqueues),
	THREAD_INITIALIZER(stack),
	THREAD_INITIALIZER(sched),
	THREAD_INITIALIZER(timer),
	THREAD_INITIALIZER(smalloc),

	/* network stack */
	THREAD_INITIALIZER(net),
	THREAD_INITIALIZER(directpath),

	/* storage */
	THREAD_INITIALIZER(storage),
};

#define LATE_INITIALIZER(name) \
	{__cstr(name), &name ## _init_late}

static const struct init_entry late_init_handlers[] = {
	/* network stack */
	LATE_INITIALIZER(arp),
	LATE_INITIALIZER(stat),
	LATE_INITIALIZER(tcp),
	LATE_INITIALIZER(rcu),
	LATE_INITIALIZER(directpath),
};

static int run_init_handlers(const char *phase,
			     const struct init_entry *h, int nr)
{
	int i, ret;

	log_debug("entering '%s' init phase", phase);
	for (i = 0; i < nr; i++) {
		log_debug("init -> %s", h[i].name);
		ret = h[i].init();
		if (ret) {
			log_debug("failed, ret = %d", ret);
			return ret;
		}
	}

	return 0;
}

static int runtime_init_thread(void)
{
	int ret;

	ret = base_init_thread();
	if (ret) {
		log_err("base library per-thread init failed, ret = %d", ret);
		return ret;
	}

	ret = run_init_handlers("per-thread", thread_init_handlers,
				 ARRAY_SIZE(thread_init_handlers));
	if (ret || perthread_init_hook == NULL)
		return ret;

	return perthread_init_hook();

}

static void *pthread_entry(void *data)
{
	int ret;

	ret = runtime_init_thread();
	BUG_ON(ret);

	pthread_barrier_wait(&init_barrier);
	pthread_barrier_wait(&init_barrier);
	sched_start();

	/* never reached unless things are broken */
	BUG();
	return NULL;
}

/* linanqinqin */
/* register lame handler via ioctl */
static int lame_init(void)
{
	int lamedev;
	struct lame_arg arg;
	int ret;
	void *handler_addr;
	int register_mode;

	if (cfg_lame_register == RT_LAME_REGISTER_NONE) {
		log_warn("WARNING: LAME handler not registered");
		return 0;
	}

	lamedev = open("/dev/lame", O_RDWR);
    if (lamedev < 0) {
        log_err("[errno %d] Failed to open /dev/lame", errno);
		return lamedev;
    }
    else {
		arg.present = 1;
		
		if (cfg_lame_tsc != LAME_TSC_OFF) {
			if (cfg_lame_bundle_size != 2) {
				log_err("LAME TSC measurement mode is only supported for bundle size 2, got %u", cfg_lame_bundle_size);
				return -EINVAL;
			}

			/* Choose handler based on TSC measurement mode */
			if (cfg_lame_tsc == LAME_TSC_PRETEND) {
				handler_addr = (void *)__lame_entry2_pretend;
			} else {
				handler_addr = (void *)__lame_entry_nop;
			}

			log_warn("WARNING: in LAME TSC measurement mode (%s)", 
					cfg_lame_tsc == LAME_TSC_PRETEND ? "pretend" : "nop");
		}
		else {
			/* Choose handler based on bundle size for optimal performance */
			if (cfg_lame_bundle_size == 2) {
				handler_addr = (void *)__lame_entry2;
			} else {
				handler_addr = (void *)__lame_entry;
			}
		}
		
		arg.handler_addr = (__u64)handler_addr;

		/* select the register mode */
		/* via INT*/
		if (cfg_lame_register == RT_LAME_REGISTER_INT) {
			register_mode = LAME_REGISTER_INT;
		/* via PMU, with LAME switching */
		} else if (cfg_lame_register == RT_LAME_REGISTER_PMU) {
			register_mode = LAME_REGISTER_PMU;
			
			if (cfg_lame_bundle_size == 2) {
				arg.handler_addr = (__u64)__lame_entry2_bret;
			} else {
				arg.handler_addr = (__u64)__lame_entry_bret;
			}
		/* via PMU, with stall emulation */
		} else if (cfg_lame_register == RT_LAME_REGISTER_STALL) {
			register_mode = LAME_REGISTER_PMU; /* pmu, stall, nop use the same kernel register */
			arg.handler_addr = (__u64)__lame_entry_stall_bret;
		/* via PMU, with nop */
		} else {
			/* nop */
			register_mode = LAME_REGISTER_PMU; /* pmu, stall, nop use the same kernel register */
			arg.handler_addr = (__u64)__lame_entry_nop_bret;
		}
		
		ret = ioctl(lamedev, register_mode, &arg);
		if (ret < 0) {
			log_err("[errno %d] ioctl LAME_REGISTER failed", errno);
		} else {
			log_notice("LAME handler registered at %p [bundle size: %u][mode: %s]", 
				(void *)arg.handler_addr, cfg_lame_bundle_size, 
				cfg_lame_register == RT_LAME_REGISTER_INT ? "int" : (cfg_lame_register == RT_LAME_REGISTER_PMU ? "pmu" : "stall"));
		}
		close(lamedev);
	}

	return ret;
}

/* end */

/**
 * runtime_set_initializers - allow runtime to specifcy a function to run in
 * each stage of intialization (called before runtime_init).
 */
int runtime_set_initializers(initializer_fn_t global_fn,
			     initializer_fn_t perthread_fn,
			     initializer_fn_t late_fn)
{
	global_init_hook = global_fn;
	perthread_init_hook = perthread_fn;
	late_init_hook = late_fn;
	return 0;
}

/**
 * runtime_init - starts the runtime
 * @cfgpath: the path to the configuration file
 * @main_fn: the first function to run as a thread
 * @arg: an argument to @main_fn
 *
 * Does not return if successful, otherwise return  < 0 if an error.
 */
int runtime_init(const char *cfgpath, thread_fn_t main_fn, void *arg)
{
	pthread_t tid[NCPU];
	int ret, i;

	ret = ioqueues_init_early();
	if (unlikely(ret))
		return ret;

	cycles_per_us = iok.iok_info->cycles_per_us;

	ret = base_init();
	if (ret) {
		log_err("base library global init failed, ret = %d", ret);
		return ret;
	}

	ret = cfg_load(cfgpath);
	if (ret)
		return ret;

	/* linanqinqin */
	/* Print the address of __lame_entry handler */
	log_info("LAME handler stub address: %p(size=2); %p(general)", (void *)__lame_entry2, (void *)__lame_entry);

	/* register lame handler via ioctl */
	ret = lame_init();
	if (ret) {
		log_warn("WARNING: LAME capability not enabled");
	}

#ifdef DEBUG
	log_info("LAME_BUNDLE_OFFSET: %lu", offsetof(struct kthread, lame_bundle));
	log_info("LAME_BUNDLE_UTHREADS: %lu", offsetof(struct lame_bundle, uthreads));
	log_info("LAME_BUNDLE_SIZE: %lu", offsetof(struct lame_bundle, size));
	log_info("LAME_BUNDLE_USED: %lu", offsetof(struct lame_bundle, used));
	log_info("LAME_BUNDLE_ACTIVE: %lu", offsetof(struct lame_bundle, active));
	log_info("LAME_BUNDLE_TOTAL_CYCLES: %lu", offsetof(struct lame_bundle, total_cycles));
	log_info("LAME_BUNDLE_TOTAL_LAMES: %lu", offsetof(struct lame_bundle, total_lames));
	log_info("LAME_BUNDLE_ENABLED: %lu", offsetof(struct lame_bundle, enabled));
	log_info("LAME_UTHREAD_WRAPPER_UTHREAD: %lu", offsetof(struct lame_uthread_wrapper, uthread));
	log_info("LAME_UTHREAD_WRAPPER_PRESENT: %lu", offsetof(struct lame_uthread_wrapper, present));
	log_info("LAME_UTHREAD_WRAPPER_CYCLES: %lu", offsetof(struct lame_uthread_wrapper, cycles));
	log_info("LAME_UTHREAD_WRAPPER_LAME_COUNT: %lu", offsetof(struct lame_uthread_wrapper, lame_count));
	log_info("LAME_UTHREAD_WRAPPER_SIZE: %lu", sizeof(struct lame_uthread_wrapper));
	log_info("THREAD_TF_OFFSET: %lu", offsetof(struct thread, tf));
#endif
	/* end */
	
	log_info("process pid: %u", getpid());

	pthread_barrier_init(&init_barrier, NULL, maxks);

	ret = run_init_handlers("global", global_init_handlers,
				ARRAY_SIZE(global_init_handlers));
	if (ret)
		return ret;

	if (global_init_hook) {
		ret = global_init_hook();
		if (ret) {
			log_err("User-specificed global initializer failed, ret = %d", ret);
			return ret;
		}
	}

	ret = runtime_init_thread();
	BUG_ON(ret);

	log_info("spawning %d kthreads", maxks);
	for (i = 1; i < maxks; i++) {
		ret = pthread_create(&tid[i], NULL, pthread_entry, NULL);
		BUG_ON(ret);
	}

	pthread_barrier_wait(&init_barrier);

	ret = ioqueues_register_iokernel();
	if (ret) {
		log_err("couldn't register with iokernel, ret = %d", ret);
		return ret;
	}

	pthread_barrier_wait(&init_barrier);

	/* point of no return starts here */

	ret = thread_spawn_main(main_fn, arg);
	BUG_ON(ret);

	ret = run_init_handlers("late", late_init_handlers,
				ARRAY_SIZE(late_init_handlers));
	BUG_ON(ret);

	if (late_init_hook) {
		ret = late_init_hook();
		if (ret) {
			log_err("User-specificed late initializer failed, ret = %d", ret);
			return ret;
		}
	}

	sched_start();

	/* never reached unless things are broken */
	BUG();
	return 0;
}
