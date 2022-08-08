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
 * @file kmem_slab.h
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
void *kmem_slaballoc(kmem_slab_zone_t *zone);

/*!
 * Release memory previously allocated with kmem_slaballoc().
 */
void kmem_slabfree(kmem_slab_zone_t *zone, void *ptr);

#endif /* KMEM_SLAB_H_ */
