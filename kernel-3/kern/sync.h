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
 * @file sync.h
 * @brief Synchronisation functionality for the kernel.
 */

#ifndef SYNCH_H_
#define SYNCH_H_

#include <sys/queue.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SPINLOCK_INITIALISER ATOMIC_FLAG_INIT

#define thread_preempt_disable()
#define thread_preempt_enable()

/*!
 * @name Spinlocks
 * @{
 */

/*! A spinlock. */
typedef volatile atomic_flag spinlock_t;

static inline void
spinlock_init(spinlock_t *lock)
{
	atomic_flag_clear(lock);
}

/*!
 * Lock a spinlock.
 * \note caller may wish to disable interrupts before this.
 */
static inline void
spinlock_lock(spinlock_t *lock)
{
	while (atomic_flag_test_and_set(lock)) {
		__asm__("pause");
	}
}

/*!
 * Unlock a spinlock.
 * \note caller may enable interrupts again after this if they wish.
 */
static inline void
spinlock_unlock(spinlock_t *lock)
{
	atomic_flag_clear(lock);
}

/*!
 * Try to lock a spinlock. If \p spin is true, then spin until it can be
 * locked. Returns 1 if lock acquired, 0 if not.
 * \note caller may wish to disable interrupts before this.
 * @returns 1 if lock acquired; 0 otherwise.
 */
static inline int
spinlock_trylock(spinlock_t *lock, bool spin)
{
	if (atomic_flag_test_and_set(lock) == 0)
		return 1;
	if (spin) {
		while (atomic_flag_test_and_set(lock)) {
			__asm__("pause");
		}
		return 1;
	}
	return 0;
}

/*!
 * Assert that a spinlock is held (of course, it doesn't assert that the current
 * thread holds it, just that the lock is held).
 */
#define ASSERT_SPINLOCK_HELD(PSL) \
	assert((atomic_flag_test_and_set(PSL)) == true)

/*!
 * @}
 */

/*!
 * @name Waitqueues
 * @{
 */

/**
 * A wait queue. Typically embedded in an object which is to be waited on, or in
 * an object which will want to wait.
 */
typedef struct waitq {
	spinlock_t lock;
	TAILQ_HEAD(, thread) waiters;
} waitq_t;

typedef enum waitq_result {
	kWQSuccess = 0,
	KWQTimeout = -1,
}waitq_result_t;

#define WAITQ_INITIALIZER(WAITQ)                                   \
	{                                                          \
		.lock = SPINLOCK_INITIALISER,                      \
		.waiters = TAILQ_HEAD_INITIALIZER((WAITQ).waiters) \
	}

/*! Await an event on a waitqueue. */
waitq_result_t waitq_await(waitq_t *wq, uint64_t nanosecs);
/*! Awaken the foremost waiter on a waitqueue. @returns 1 if a thread woke. */
int waitq_wake_one(waitq_t *wq);

/*!
 * @}
 */

/*!
 * @name Mutexes
 * @{
 */

typedef struct mutex {
	struct thread *_Atomic owner;
	waitq_t		wq;
	atomic_uint	count;
	spinlock_t	lock;
} mutex_t;

#define MUTEX_INITIALISER(MUTEX)                                          \
	{                                                                 \
		.wq = WAITQ_INITIALIZER((MUTEX).wq), .owner = NULL, \
		.count = 0, .lock = SPINLOCK_INITIALISER                  \
	}

static inline void
mutex_init(mutex_t *mtx)
{
	*mtx = (mutex_t)MUTEX_INITIALISER(*mtx);
};
void mutex_lock(mutex_t *mtx);
void mutex_unlock(mutex_t *mtx);
#define ASSERT_MUTEX_HELD(PMTX) \
	assert((PMTX)->owner == curthread())
/*!
 * @}
 */

#endif /* SYNCH_H_ */
