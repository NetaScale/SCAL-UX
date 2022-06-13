#ifndef PROCESS_H_
#define PROCESS_H_

#include "kern/queue.h"
#include "kern/vm.h"
#include "pcb.h"

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
	/** queue of runnable threads */
	TAILQ_HEAD(, thread) runqueue;
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

extern TAILQ_HEAD(allprocs, process) allprocs;
extern spinlock_t process_lock;
extern process_t proc0;
extern cpu_t *cpus;
extern size_t ncpus;

#endif /* PROCESS_H_ */
