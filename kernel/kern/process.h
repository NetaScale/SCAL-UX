#ifndef PROCESS_H_
#define PROCESS_H_

#include "amd64.h" /* for curcpu */
#include "kern/queue.h"
#include "kern/vm.h"
#include "pcb.h"
#include "waitq.h"

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
	TAILQ_ENTRY(callout) entries;
	void (*fun)(void *arg);
	void *arg;
	/**
	 * time (relative to now if this is the head of the callout queue,
	 * otherwise relative to previous callout) in milliseconds till expiry
	 */
	uint64_t timeout;
} callout_t;

typedef struct cpu {
	uint64_t num; /* unique CPU id */
	/* TODO: portability */
	spinlock_t lock;
	uint64_t lapic_id;
	uint64_t lapic_tps; /* lapic ticks per second for divider 16 */
	tss_t *tss; /* points into a static structure right now - TODO allow
			allocations contained within a single page */
	uint64_t counter;   /* per-cpu counter */
	uint64_t timeslice; /* remaining timeslice of current thread */
	/* end todos */
	/** currently-running thread */
	struct thread *curthread;

	/**
	 * Queue of runnable threads. Needs SPL soft and process_lock.
	 */
	TAILQ_HEAD(, thread) runqueue;

	/**
	 * Queue of waiting threads. Needs SPL soft and process_lock.
	 */
	TAILQ_HEAD(, thread) waitqueue;

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
	/* For cpu_t::runqueues/waitqueue or ::exited_threads. */
	TAILQ_ENTRY(thread) runqueue;
	/* For process::threads. */
	LIST_ENTRY(thread) threads;

	spinlock_t lock;

	enum {
		kRunnable,
		kRunning,
		kWaiting,
		/** destruction will be enqueued on next reschedule */
		kExiting,
	} state;

	/** whether the thread should exit asap */
	bool should_exit : 1;
	/** whether the thread is in a system call */
	bool in_syscall : 1;

	/** CPU to which thread is bound */
	cpu_t *cpu;

	/* per-arch process control block */
	pcb_t pcb;
	/* kernel thread or user? */
	bool kernel;
	vaddr_t kstack;
	/* process to which it belongs */
	struct process *proc;

	/* waitq on which the thread is currently waiting */
	waitq_t *wq;
	/* event on which the thread is waiting from the waitq */
	waitq_event_t wqev;
	/* result of the wait */
	waitq_result_t wqres;
	/* entry in wq */
	waitq_entry_t wqent;
	/* timeout for wq wait */
	callout_t wqtimeout;
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
	struct posix_proc *pxproc;

	/* Threads belonging to this process. */
	LIST_HEAD(, thread) threads;
} process_t;

/**
 * Enqueue a callout onto this CPU's queue.
 */
void callout_enqueue(callout_t *callout);

/**
 * Dequeue a callout from this CPU's queue.
 */
void callout_dequeue(callout_t *callout);

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
 * Create a new kernel thread with the given entry point and single argument.
 */
void thread_new_kernel(void *entry, void *arg);

/**
 * Block current thread to await a result from its waitq.
 *
 * \pre \p thread is locked
 */
waitq_result_t thread_block_locked();

/*
 * Launch a thread. It is set runnable and placed in an appropriate runqueue.
 */
void thread_run(thread_t *thread);

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
