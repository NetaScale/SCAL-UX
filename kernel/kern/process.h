#ifndef PROCESS_H_
#define PROCESS_H_

#include "kern/queue.h"
#include "kern/vm.h"
#include "pcb.h"

/*
 * Deferred Procedure Calls, inspired by those of Windows NT and playing a
 * similar role to NetBSD's softints. They are run when SPL drops to soft or
 * lower. They are
 */
typedef struct dpc {
	/* for cpu_t::dpcqueue */
	TAILQ_ENTRY(dpc) dpcqueue;

	void (*fun)(void *arg);
	void *arg;
	/* whether it's on a cpu_t's queue. TODO: check TAILQ_ENTRY instead? */
	bool bound;
} dpc_t;

/**
 * A callout is the most fundamental sort of timer available. They are processed
 * by a DPC.
 */
typedef struct callout {
	/** for cpu_t::pendingcallouts or cpu_t::elapsedcallouts */
	TAILQ_ENTRY(callout) queue;
	void (*fun)(void *arg);
	void *arg;
} callout_t;

typedef struct cpu {
	uint64_t num; /* unique CPU id */
	/* TODO: portability */
	spinlock_t lock;
	uint64_t lapic_id;
	uint64_t lapic_tps; /* lapic ticks per second for divider 16 */
	tss_t *tss; /* points into a static structure right now - TODO allow
			allocations contained within a single page */
	uint64_t counter; /* per-cpu counter */
	/* end todos */
	/** currently-running thread */
	struct thread *curthread;

	/**
	 * Queue of runnable threads. Needs SPL soft and process_lock.
	 */
	TAILQ_HEAD(, thread) runqueue;

	/**
	 * Queue of pending DPCs. Needs SPL high.
	 */
	TAILQ_HEAD(, dpc) dpcqueue;

	/*
	 * The callout expiry handler DPC. Responsible for calling the handlers
	 * of callouts.
	 */
	dpc_t calloutdpc;

	/**
	 * Queue of pending callouts. Needs SPL high. Emptied by the clock ISR.
	 */
	TAILQ_HEAD(, callout) pendingcallouts;

	/**
	 * Queue of elapsed callouts. Needs SPL high. Filled by the clock ISR,
	 * emptied by the calloutdpc DPC.
	 */
	TAILQ_HEAD(, callout) elapsedcallouts;
} cpu_t;

typedef struct thread {
	/* For cpu_t::runqueue. */
	TAILQ_ENTRY(thread) runqueue;
	/* For process::threads. */
	LIST_ENTRY(thread) threads;

	/* per-arch process control block */
	pcb_t pcb;
	/* kernel thread or user? */
	bool kernel;
	vaddr_t kstack;
	/* process to which it belongs */
	struct process *proc;
} thread_t;

/*
 * A VXK process.
 */
typedef struct process {
	/* For ::allprocs. */
	TAILQ_ENTRY(process) allprocs;

	vm_map_t *map;
	int pid;
	char name[31];

	/* Posix subsystem process, may be NULL if process not Posixy */
	struct posix_proc *pproc;

	/* Threads belonging to this process. */
	LIST_HEAD(, thread) threads;
} process_t;

/*
 * Fork a process to create a new one. The new process inherits the parents'
 * address space (according to map entry's inheritance settings) but is
 otherwise
 */
process_t *process_new(process_t *parent);

/*
 * Create a new (empty) thread in a process. It can be started with
 * thread_run().
 */
thread_t *thread_new(process_t *proc, bool iskernel);

/*
 * Launch a thread. It is set runnable and placed in an appropriate runqueue.
 */
void thread_run(thread_t *thread);

/*
 * Create a new kernel thread with the given entry point and single argument.
 * The thread is immediately placed on a CPU's runqueue.
 */
void thread_new_kernel(void *entry, void *arg);

/**
 * @section internal
 */
/* run pending callouts; called only as part of the calloutdpc */
void callouts_run(void *arg);
/* run pending DPCs; called only by the scheduler */
void dpcs_run();

extern TAILQ_HEAD(allprocs, process) allprocs;
extern spinlock_t process_lock;
extern process_t proc0;
extern cpu_t *cpus;
extern size_t ncpus;

#endif /* PROCESS_H_ */
