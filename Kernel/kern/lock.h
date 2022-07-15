/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#ifndef LOCKS_H_
#define LOCKS_H_

#include <stdatomic.h>

typedef volatile atomic_flag spinlock_t;

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

#define SPINLOCK_INITIALISER ATOMIC_FLAG_INIT

#endif /* LOCKS_H_ */
