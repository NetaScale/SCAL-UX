/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

/**
 * @file task.h
 * @brief Tasks and threads.
 */

#ifndef TASK_H_
#define TASK_H_

#include <machine/intr.h>
#include <machine/machdep.h>
#include <vm/vm.h>

typedef struct callout {
        /* links cpu::pendingcallouts */
	TAILQ_ENTRY(callout) queue;
	void (*callback)(md_intr_frame_t *frame, void *arg);
	void *arg;

	/*
	 * time (relative to now if this is the head of the callout queue,
	 * otherwise relative to previous callout) in nanoseconds till expiry
	 */
	uint64_t nanosecs;
	enum {
		kCalloutDisabled, /**< not enqueued */
		kCalloutPending,  /**< pending timeout */
	} state;
} callout_t;

typedef struct task {
	char	  name[31];
	vm_map_t *map;

	/*! linked by thread::taskthreads */
	SLIST_HEAD(, thread) threads;
} task_t;

enum thread_state {
	kThreadRunnable = 0,
	kThreadRunning,
	kThreadWaiting,
	kThreadExiting,
};

/*!
 * Locks:
 * @var lock Protects structures accessed by scheduler (l).
 */

typedef struct thread {
	/*! linkage for task::threads */
	SLIST_ENTRY(thread) taskthreads;
	/*! linkage for cpu::runqueue or a wait queue or... */
	TAILQ_ENTRY(thread) queue;

	/*! @name scheduling @{ */
	/*! protects scheduling structures */
	spinlock_t lock;
	/*! [l] wait queue (if any) on which the thread is blocking */
	waitq_t *wq;
	/*! [l] result of wait */
	waitq_result_t wqres;
	/*! [l] CPU to which the thread the belongs. */
	struct cpu *cpu;
	/*! [l] current running state of thread */
	enum thread_state state;
	/*! @} */

	/*! kernal stack */
	vaddr_t kstack;
	/*! user-mode stack (or NULL) */
	vaddr_t ustack;

	/*! task to which the thread belongs */
	task_t *task;

	/*! machine-dependent thread block */
	md_thread_t md;
} thread_t;

typedef struct cpu {
	int	  num;
	thread_t *curthread;

	thread_t *idlethread;

	/*!
         * Run-queue of threads. Linked by thread::queue.
         */
	TAILQ_HEAD(, thread) runqueue;

	/*! Whether to reschedule on dropping priority/finishing interrupt. */
	bool preempted : 1;

	/*! The timeslicing callout - timeslices processes. */
	callout_t timeslicer;

	/**
	 * Queue of pending callouts. Linked by callout::queue. Needs interrupts
	 * off (??and cpu lock in the future??). Emptied by local timer ISR.
	 */
	TAILQ_HEAD(, callout) pendingcallouts;

	/*! machine-dependent cpu block */
	md_cpu_t md;
} cpu_t;

static inline thread_t *
curthread()
{
	return curcpu()->curthread;
}

static inline task_t *
curtask()
{
	return curcpu()->curthread->task;
}

/*! Enqueue a callout for running. */
void callout_enqueue(callout_t *callout);
/*! Dequeue and disable a callout. */
void callout_dequeue(callout_t *callout);
/*! Private - callout interrupt handler. */
void callout_interrupt(md_intr_frame_t *frame, void *unused);

/*! Create a new thread; it is assigned a CPU but not enqueued for running. */
thread_t *thread_new(task_t *task, void (*fun)(void *arg), void *arg);
/*! Resume a suspended thread; it may preempt the currently running. */
void thread_resume(thread_t *thread);

/*! Private - called by the timeslicer callout when a timeslice expires. */
void sched_timeslice(md_intr_frame_t *frame, void *arg);
/*!
 * Reschedule to a new thread (if there are any eligible candidates.) If the
 * current thread wants to sleep or exit, it should have changed its state to
 * kThreadWaiting or whichever state is relevant to it. If it is simply yielding
 * then it should not change its state.
 */
void sched_reschedule(void);

extern task_t	task0;
extern thread_t thread0;
extern cpu_t	cpu0;
extern cpu_t  **cpus;
extern int	ncpu;

#endif /* TASK_H_ */
