/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

/*!
 * @file kmem.h
 * @brief Implementation of a slab allocator and a generic kmalloc in terms of
 * it.
 */

#ifndef KMEM_SLAB_H_
#define KMEM_SLAB_H_

#include <sys/types.h>

#ifndef _KERNEL
#define mutex_t int
#else
#include <kern/sync.h>
#endif

/*!
 * A KMem slab zone - provides slab allocation for a particular size of object.
 */
typedef struct kmem_slab_zone {
	/*! identifier name */
	const char *name;
	/*! size of contained objects */
	size_t size;
	/*! queue of slabs */
	SIMPLEQ_HEAD(, kmem_slab) slablist;
	/*! locking */
	mutex_t lock;

	/*! the below are applicable only to large slabs */
	/*! list of allocated bufctls. TODO(med): Use a hash table? */
	SLIST_HEAD(, kmem_bufctl) bufctllist;
} kmem_slab_zone_t;

/*! Initialise the KMem system. */
void kmem_init(void);

/*!
 * Allocate from a slab zone.
 */
void *kmem_zonealloc(kmem_slab_zone_t *zone);

/*!
 * Release memory previously allocated with kmem_slaballoc().
 */
void kmem_zonefree(kmem_slab_zone_t *zone, void *ptr);

/*!
 * Allocate kernel wired memory. Memory will be aligned to zone's size (thus
 * power-of-2 allocations will be naturally aligned).
 */
void *kmem_alloc(size_t size);

/*!
 * Release memory allocated by kmem_alloc(). \p size must match the size that
 * was allocated by kmem_alloc or kmem_realloc.
 */
void kmem_free(void *ptr, size_t size);

/*!
 * Allocate kernel wired memory generically; this is a compatibility interface
 * for those who aren't able to provide the size of allocation when freeing the
 * allocated memory. Alignment is to 8 bytes only.
 */
void *kmem_genalloc(size_t size);

/*!
 * Release memory allocated by kmem_genalloc().
 */
void kmem_genfree(size_t size);

#endif /* KMEM_SLAB_H_ */
