#ifndef PROCESS_H_
#define PROCESS_H_

#include "kern/queue.h"
#include "kern/vm.h"
#include "pcb.h"

typedef struct cpu {
	uint64_t num;
	/* TODO: portability */
	uint64_t lapic_id;
	uint64_t lapic_tps; /* lapic ticks per second for divider 16 */
	tss_t tss;
	/* end todos */
	struct thread *curthread;
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
	/* process to which it belongs */
	struct process *proc;
} thread_t;

/*
 * A VXK process.
 */
typedef struct process {
	/* For allprocs. */
	TAILQ_ENTRY(process) allprocs;

	vm_map_t *map;
	int pid;
	char name[31];

	/* Threads belonging to this process. */
	LIST_HEAD(, thread) threads;
} process_t;

extern TAILQ_HEAD(allprocs, process) allprocs;
extern spinlock_t process_lock;
extern process_t proc0;

#endif /* PROCESS_H_ */
