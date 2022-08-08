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
 * @name Mutexes
 * @{
 */

typedef struct mutex {

} mutex_t;

static inline void mutex_lock(mutex_t *mtx) {};
static inline void mutex_unlock(mutex_t *mtx) {};
static inline void mutex_unlock(mutex_t *mtx) {};
#define ASSERT_MUTEX_HELD(PMTX) \
	assert(true)
/*!
 * @}
 */

#endif /* SYNCH_H_ */
