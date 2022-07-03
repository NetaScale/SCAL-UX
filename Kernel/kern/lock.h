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

typedef volatile int spinlock_t;

static inline void
lock(spinlock_t *lock)
{
	while (!__sync_bool_compare_and_swap(lock, 0, 1)) {
		while (*lock)
			__asm__("pause");
	}
}

static inline void
unlock(spinlock_t *lock)
{
	__sync_lock_release(lock);
}

#endif /* LOCKS_H_ */
