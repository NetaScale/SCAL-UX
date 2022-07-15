/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

/**
 * @file task.h
 * @brief Scheduler, CPU description, task, thread, etc.
 */

#ifndef TASK_H_
#define TASK_H_

#include <sys/queue.h>

#include <machine/pcb.h>
#include <machine/spl.h>

#include <stdbool.h>

#include "lock.h"
#include "vm.h"

#define KERNEL_STACK_SIZE 4096 * 8
#define USER_STACK_SIZE 4096 * 8

#define NS_PER_MS 1000000

/** A particular event identifier within a wait queue. */
typedef uintptr_t waitq_event_t;

/** Result of a wait operation on a wait queue. */
typedef enum waitq_result {
	kWaitQResultWaiting = -1,
	kWaitQResultTimeout,
	kWaitQResultInterrupted,
	kWaitQResultEvent,
} waitq_result_t;

/**
 * A wait queue. Embedded in an object which may be waited on.
 */
typedef struct waitq {
	spinlock_t lock;
	TAILQ_HEAD(, thread) waiters;
} waitq_t;

/*
 * Deferred Procedure Calls, inspired by those of Windows NT and playing a
 * similar role to NetBSD's softints. They are run when SPL drops to low; SPL
 * is raised to soft to run them.
 */
typedef struct dpc {
	/* for cpu_t::dpcqueue */
	TAILQ_ENTRY(dpc) dpcqueue;

	void (*fun)(void *arg);
	void *arg;
	bool  bound; /** whether it's enqueued to be run */
} dpc_t;

/**
 * A callout is the most fundamental sort of timer available. They are processed
 * by a DPC.
 */
typedef struct callout {
	/** links cpu_t::pendingcallouts or cpu_t::elapsedcallouts */
	TAILQ_ENTRY(callout) entries;
	/** dpc to be enqueued on elapsing */
	dpc_t dpc;
	/**
	 * time (relative to now if this is the head of the callout queue,
	 * otherwise relative to previous callout) in nanoseconds till expiry
	 */
	uint64_t nanosecs;
	enum {
		kCalloutDisabled, /**< not enqueued */
		kCalloutPending,  /**< pending timeout */
		kCalloutElapsed,  /**< timeout elapsed */
	} state;
} callout_t;

typedef struct cpu {
	uint64_t       num;	  /** unique CPU id */
	struct thread *curthread; /** currently-running thread */

	/** Arch-specific CPU description. */
	arch_cpu_t arch_cpu;

	/** CPU's idle thread. */
	struct thread *idlethread;

	/** Rescheduling timer, incorporating the DPC. */
	callout_t resched;

	/**
	 * Queue of runnable threads. Needs SPL soft and process_lock.
	 */
	TAILQ_HEAD(, thread) runqueue;

	/**
	 * Queue of waiting threads. Needs SPL soft and process_lock.
	 */
	TAILQ_HEAD(, thread) waitqueue;

	/**
	 * Queue of pending DPCs. Needs SPL sched.
	 */
	TAILQ_HEAD(, dpc) dpcqueue;

	/**
	 * Queue of pending callouts. Needs SPL sched. Emptied by the clock ISR.
	 */
	TAILQ_HEAD(, callout) pendingcallouts;

} cpu_t;

typedef struct thread {
	/** For cpu_t::runqueues/waitqueue or ::exited_threads. */
	TAILQ_ENTRY(thread) runqueue;
	/** For waitq_t::waiters */
	TAILQ_ENTRY(thread) waitqueue;
	/** For process::threads. */
	LIST_ENTRY(thread) threads;

	spinlock_t lock;

	enum {
		kRunnable = 0,
		kRunning,
		kWaiting,
		/** destruction will be enqueued on next reschedule */
		kExiting,
	} state;

	/** whether the thread has a signal to process */
	bool signalled : 1;
	/** whether the thread is in a system call */
	bool in_syscall : 1;

	/** CPU to which thread is bound */
	cpu_t *cpu;

	/* per-arch process control block */
	arch_pcb_t pcb;
	/* kernel thread or user? */
	bool kernel;
	/* if a user process, its kernel stack base address */
	vaddr_t kstack;
	/* if a user process, its stack base address */
	vaddr_t stack;
	/* task to which it belongs */
	struct task *task;

	/* waitq on which the thread is currently waiting */
	waitq_t *wq;
	/* event on which the thread is waiting from the waitq */
	waitq_event_t wqev;
	/* result of the wait */
	waitq_result_t wqres;
	/* timeout for wq wait */
	callout_t wqtimeout;
} thread_t;

typedef struct task {
	/* For ::alltasks. */
	TAILQ_ENTRY(task) allprocs;

	char	  name[31];
	int	  pid;
	vm_map_t *map;

	/* Posix subsystem process, may be NULL if task not Posixy */
	struct proc *pxproc;

	/* Threads belonging to this task. */
	LIST_HEAD(, thread) threads;
} task_t;

TAILQ_HEAD(task_queue, task);

/** Enqueue a DPC for running. */
void dpc_enqueue(dpc_t *dpc);

/** Enqueue a callout for running. */
void callout_enqueue(callout_t *callout);
/** Dequeue and disable a callout. */
void callout_dequeue(callout_t *callout);

/** Create a new thread; it is assigned a CPU but not enqueued for running. */
thread_t *thread_new(task_t *proc, bool iskernel);
/** Set a thread to run a function with an argument. */
void thread_goto(thread_t *thr, void (*fun)(void *), void *arg);
/** Mark a thread runnable; it may preempt the currently running. */
void thread_run(thread_t *thread);

/** Await an event on a waitq. */
waitq_result_t waitq_await(waitq_t *wq, waitq_event_t ev, uint64_t nanosecs);
/** Initialise a waitq. */
void waitq_init(waitq_t *wq);

/**
 * Clear wait-state on a thread with a particular result. ~~Both the thread
 * and~~ the waitq should be locked. The thread is neither rescheduled nor set
 * runnable.
 */
void thread_clearwait_locked(struct thread *thread, waitq_event_t ev,
    waitq_result_t res);

/**
 * Wake one waiter on a waitqueue.
 * \pre SPL soft
 */
void waitq_wake_one(waitq_t *wq, waitq_event_t ev);

static inline task_t *
CURTASK()
{
	return CURCPU()->curthread->task;
}
static inline thread_t *
CURTHREAD()
{
	return CURCPU()->curthread;
}

extern spinlock_t	 sched_lock;
extern struct task_queue alltasks;
extern task_t		 task0;
extern cpu_t	     *cpus;
extern size_t		 ncpus;

#endif /* TASK_H_ */
