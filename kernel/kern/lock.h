/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#ifndef LOCK_H_
#define LOCK_H_

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/queue.h>


typedef volatile atomic_flag spinlock_t;

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


struct mutex {
	waitq_t	  waitq;
	struct thread *holder;
	size_t	  count;
};

typedef struct mutex	     mutex_t;

void mutex_init(mutex_t *mtx);
void mutex_lock(mutex_t *mtx);
void mutex_unlock(mutex_t *mtx);

static inline void
spinlock_init(spinlock_t *lock)
{
	atomic_flag_clear(lock);
}

static inline void
lock(spinlock_t *lock)
{
	while (atomic_flag_test_and_set(lock)) {
		__asm__("pause");
	}
}

static inline void
unlock(spinlock_t *lock)
{
	atomic_flag_clear(lock);
}

/**
 * Try to lock a spinlock. If \p spin is true, then spin until it can be
 * locked. Returns 1 if lock acquired, 0 if not.
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

#define SPINLOCK_INITIALISER ATOMIC_FLAG_INIT

#endif /* LOCK_H_ */
