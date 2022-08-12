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
 * \page kmem_slab KMem Slab Allocator
 *
 * See: Bonwick, J. (1994). The Slab Allocator: An Object-Caching Kernel Memory
 * Allocator.
 *
 * Overview
 * ========
 *
 * Implementation
 * ==============
 *
 * There are two formats of a slab: a small slab and a large slab.

 * Small slabs are for objects smaller than or equal to PGSIZE / 16. They are
 * one page in size, and consistent of objects densely packed, with the struct
 * kmem_slab header occupying the top bytes of the page.
 *
 * Objects and slab_bufctls are united in small slabs - since a slab is always
 * exactly PGSIZE in length, there is no need to look up a bufctl in the zone;
 * instead it can be calculated trivially. So an object slot in a small slab is
 * a bufctl linked into the freelist when it is free; otherwise it is the
 * object. This saves on memory expenditure (and means that bufctls for large
 * slabs can themselves be slab-allocated).
 *
 * Large slabs have out-of-line slab headers and bufctls, and their bufctls have
 * a back-pointer to their containing slab as well as their base address. To
 * free an object in a large zone requires to look up the bufctl; their bufctls
 * are therefore linked into a list of allocated bufctls in the kmem_zone.
 * [in the future this will be a hash table.]
 */
#include <sys/queue.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef _KERNEL
#include <kern/kmem.h>
#include <kern/vm.h>
#include <libkern/klib.h>
#else
#include <sys/mman.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kmem.h"

#define PGSIZE 4096
#define ROUNDUP(addr, align) (((addr) + align - 1) & ~(align - 1))
#define ROUNDDOWN(addr, align) ((((uintptr_t)addr)) & ~(align - 1))
#define PGROUNDUP(addr) ROUNDUP(addr, PGSIZE)
#define PGROUNDDOWN(addr) ROUNDDOWN(addr, PGSIZE)
#define kVMKSleep 0

#define mutex_lock(...)
#define mutex_unlock(...)

#define kprintf(...) printf(__VA_ARGS__)
#define fatal(...)                   \
	({                           \
		printf(__VA_ARGS__); \
		exit(1);             \
	})

static inline void *
vm_kalloc(int npages, int unused)
{
	void *ret;
	assert(posix_memalign(&ret, PGSIZE * npages, PGSIZE) == 0);
	return ret;
}
#endif

/*!
 * Bufctl entry. These are stored inline for small slabs (and are replaced by
 * the object when an object is allocated) and out-of-line for large slabs (this
 * enables denser packing, since the data in the slab is now possible to pack
 * densely; it also facilitates alignment.)
 *
 * Note that only the first entry is present in all bufctls; only large bufctls
 * have the other two.
 */
struct kmem_bufctl {
	/*!
	 * Linkage either for free list (only case for small slab); or for large
	 * slabs, kmem_zone::bufctllist
	 */
	SLIST_ENTRY(kmem_bufctl) entrylist;

	/*! slab to which bufctl belongs */
	struct kmem_slab *slab;
	/*! absolute address of entry */
	char *base;
};

/*!
 * A single slab.
 */
struct kmem_slab {
	/*! linkage kmem_zone::slablist */
	SIMPLEQ_ENTRY(kmem_slab) slablist;
	/*! zone to which it belongs */
	struct kmem_zone *zone;
	/*! number of free entries */
	uint32_t nfree;
	/*! first free bufctl */
	struct kmem_bufctl *firstfree;
	/*!
	 * For a small slab, slab contents precede this structure. Large slabs
	 * however have a pointer to their data here.
	 */
	void *data[0];
};

/*!
 * Get the address of a small slab's header from the base address of the slab.
 */
#define SMALL_SLAB_HDR(BASE) ((BASE) + PGSIZE - sizeof(struct kmem_slab))

/*! Maximum size of object that will be stored in a small slab. */
const size_t kSmallSlabMax = 256;

/*!
 * 8-byte granularity <= 64 byte;
 * 16-byte granularity <= 128 byte;
 * 32-byte granularity <= 256 byte;
 * 64-byte granularity <= 512 byte;
 * 128-byte granularity <= 1024 byte;
 * 256-byte granularity <= 2048 byte;
 * 512-byte granularity < 4096 byte;
 * >=4096 byte allocations are directly carried out by vm_kalloc() so
 * granularity is 4096 bytes.
 */
#define ZONE_SIZES(X)      \
	X(8, kmem_8)       \
	X(16, kmem_16)     \
	X(24, kmem_24)     \
	X(32, kmem_32)     \
	X(40, kmem_40)     \
	X(48, kmem_48)     \
	X(56, kmem_56)     \
	X(64, kmem_64)     \
	X(80, kmem_80)     \
	X(96, kmem_96)     \
	X(112, kmem_112)   \
	X(128, kmem_128)   \
	X(160, kmem_160)   \
	X(192, kmem_192)   \
	X(224, kmem_224)   \
	X(256, kmem_256)   \
	X(320, kmem_320)   \
	X(384, kmem_384)   \
	X(448, kmem_448)   \
	X(512, kmem_512)   \
	X(640, kmem_640)   \
	X(768, kmem_768)   \
	X(896, kmem_896)   \
	X(1024, kmem_1024) \
	X(1280, kmem_1280) \
	X(1536, kmem_1536) \
	X(1792, kmem_1792) \
	X(2048, kmem_2048) \
	X(2560, kmem_2560) \
	X(3072, kmem_3072) \
	X(3584, kmem_3584) \
	X(4096, kmem_4096)

/*! struct kmem_slab's for large-slab zones */
static struct kmem_zone kmem_slab;
/*! struct kmem_bufctl's for large-slab zones */
static struct kmem_zone kmem_bufctl;
/* general-purpose zones for kmem_alloc */
#define DEFINE_ZONE(SIZE, NAME) static struct kmem_zone NAME;
ZONE_SIZES(DEFINE_ZONE);
#undef DEFINE_ZONE
/*! array of the kmem_alloc zones for convenience*/
#define REFERENCE_ZONE(SIZE, NAME) &NAME,
static kmem_zone_t *kmem_alloc_zones[] = { ZONE_SIZES(REFERENCE_ZONE) };
#undef REFERENCE_ZONE
/*! list of all zones; TODO(med): protect with a lock */
struct kmem_zones kmem_zones = SIMPLEQ_HEAD_INITIALIZER(kmem_zones);


void
kmem_zone_init(struct kmem_zone *zone, const char *name, size_t size)
{
	zone->name = name;
	zone->size = size;
	SIMPLEQ_INIT(&zone->slablist);
	SLIST_INIT(&zone->bufctllist);
	SIMPLEQ_INSERT_TAIL(&kmem_zones, zone, zonelist);
}

void
kmem_init(void)
{
	kmem_zone_init(&kmem_slab, "kmem_slab",
	    sizeof(struct kmem_slab) + sizeof(void *));
	kmem_zone_init(&kmem_bufctl, "kmem_bufctl",
	    sizeof(struct kmem_bufctl));
#define ZONE_INIT(SIZE, NAME) kmem_zone_init(&NAME, #NAME, SIZE);
	ZONE_SIZES(ZONE_INIT);
#undef ZONE_INIT
}

/* return the size in bytes held in a slab of a given zone*/
static size_t
slabsize(kmem_zone_t *zone)
{
	if (zone->size <= kSmallSlabMax) {
		return PGSIZE;
	} else {
		/* aim for at least 16 entries */
		return PGROUNDUP(zone->size * 16);
	}
}

/* return the capacity in number of objects of a slab of this zone */
static uint32_t
slabcapacity(kmem_zone_t *zone)
{
	if (zone->size <= kSmallSlabMax) {
		return (slabsize(zone) - sizeof(struct kmem_slab)) / zone->size;
	} else {
		return slabsize(zone) / zone->size;
	}
}

static struct kmem_slab *
small_slab_new(kmem_zone_t *zone)
{
	struct kmem_slab	 *slab;
	struct kmem_bufctl *entry = NULL;
	void	       *base;

	/* create a new slab */
	base = vm_kalloc(1, kVMKSleep);
	slab = SMALL_SLAB_HDR(base);

	SIMPLEQ_INSERT_HEAD(&zone->slablist, slab, slablist);

	slab->zone = zone;
	slab->nfree = slabcapacity(zone);

	/* set up the freelist */
	for (size_t i = 0; i < slabcapacity(zone); i++) {
		entry = (struct kmem_bufctl *)(base + i * zone->size);
		entry->entrylist.sle_next = (struct kmem_bufctl *)(base +
		    (i + 1) * zone->size);
	}
	entry->entrylist.sle_next = NULL;
	slab->firstfree = (struct kmem_bufctl *)(base);

	return slab;
}

static struct kmem_slab *
large_slab_new(kmem_zone_t *zone)
{
	struct kmem_slab	 *slab;
	struct kmem_bufctl *entry = NULL, *prev = NULL;

	slab = kmem_zonealloc(&kmem_slab);

	SIMPLEQ_INSERT_HEAD(&zone->slablist, slab, slablist);
	slab->zone = zone;
	slab->nfree = slabcapacity(zone);
	slab->data[0] = vm_kalloc(slabsize(zone) / PGSIZE, kVMKSleep);

	/* set up the freelist */
	for (size_t i = 0; i < slabcapacity(zone); i++) {
		entry = kmem_zonealloc(&kmem_bufctl);
		entry->slab = slab;
		entry->base = slab->data[0] + zone->size * i;
		if (prev)
			prev->entrylist.sle_next = entry;
		else {
			/* this is the first entry */
			slab->firstfree = entry;
		}
		prev = entry;
	}
	entry->entrylist.sle_next = NULL;

	return slab;
}

void *
kmem_zonealloc(kmem_zone_t *zone)
{
	struct kmem_bufctl *entry, *next;
	struct kmem_slab	 *slab;
	void	       *ret;

	mutex_lock(&zone->lock);

	slab = SIMPLEQ_FIRST(&zone->slablist);
	if (!slab || slab->nfree == 0) {
		/* no slabs or all full (full slabs always at tail of queue) */
		if (zone->size > kSmallSlabMax) {
			slab = large_slab_new(zone);
		} else {
			if (slab)
				for (;;) ;
			slab = small_slab_new(zone);
		}
	}

	__atomic_sub_fetch(&slab->nfree, 1, __ATOMIC_RELAXED);
	entry = slab->firstfree;

	next = entry->entrylist.sle_next;
	if (next == NULL) {
		/* slab is now empty; put it to the back of the slab queue */
		SIMPLEQ_REMOVE(&zone->slablist, slab, kmem_slab, slablist);
		SIMPLEQ_INSERT_TAIL(&zone->slablist, slab, slablist);
		slab->firstfree = NULL;
	} else {
		slab->firstfree = next;
	}

	if (zone->size <= kSmallSlabMax) {
		ret = (void *)entry;
	} else {
		SLIST_INSERT_HEAD(&zone->bufctllist, entry, entrylist);
		ret = entry->base;
	}
	mutex_unlock(&zone->lock);
	return ret;
}

void
kmem_zonefree(kmem_zone_t *zone, void *ptr)
{
	struct kmem_slab	 *slab;
	struct kmem_bufctl *newfree;

	mutex_lock(&zone->lock);

	if (zone->size <= kSmallSlabMax) {
		slab = (struct kmem_slab *)SMALL_SLAB_HDR(PGROUNDDOWN(ptr));
		newfree = (struct kmem_bufctl *)ptr;
	} else {
		struct kmem_bufctl *iter;

		SLIST_FOREACH(iter, &zone->bufctllist, entrylist)
		{
			if (iter->base == ptr) {
				newfree = iter;
				break;
			}
		}

		if (!newfree) {
			fatal("kmem_slabfree: invalid pointer %p", ptr);
			return;
		}

		SLIST_REMOVE(&zone->bufctllist, newfree, kmem_bufctl,
		    entrylist);
		slab = iter->slab;
	}

	slab->nfree++;
	/* TODO: push slab to front; if nfree == slab capacity, free the slab */
	newfree->entrylist.sle_next = slab->firstfree;
	slab->firstfree = newfree;

	mutex_unlock(&zone->lock);
}

void
kmem_dump()
{
	kmem_zone_t *zone;

	kprintf("\033[7m%-24s%-6s%-6s\033[m\n", "name", "size", "objs");

	SIMPLEQ_FOREACH(zone, &kmem_zones, zonelist)
	{
		size_t		  cap;
		size_t		  nSlabs = 0;
		size_t		  totalFree = 0;
		struct kmem_slab *slab;

		mutex_lock(&zone->lock);

		cap = slabcapacity(zone);

		SIMPLEQ_FOREACH(slab, &zone->slablist, slablist)
		{
			nSlabs++;
			totalFree += slab->nfree;
		}

		kprintf("%-24s%-6zu%-6lu\n", zone->name, zone->size,
		    cap * nSlabs - totalFree);

		mutex_unlock(&zone->lock);
	}
}

static inline int
zonenum(size_t size)
{
	if (size <= 64)
		return ROUNDUP(size, 8) / 8 - 1;
	else if (size <= 128)
		return ROUNDUP(size - 64, 16) / 16 + 7;
	else if (size <= 256)
		return ROUNDUP(size - 128, 32) / 32 + 11;
	else if (size <= 512)
		return ROUNDUP(size - 256, 64) / 64 + 15;
	else if (size <= 1024)
		return ROUNDUP(size - 512, 128) / 128 + 19;
	else if (size <= 2048)
		return ROUNDUP(size - 1024, 256) / 256 + 23;
	else if (size <= 4096)
		return ROUNDUP(size - 2048, 512) / 512 + 27;
	else
		/* use vm_kalloc() directly */
		return -1;
}

void *
kmem_alloc(size_t size)
{
	int zoneidx;

	assert(size > 0);
	zoneidx = zonenum(size);

	if (zoneidx == -1) {
		size_t realsize = PGROUNDUP(size);
		return vm_kalloc(realsize / PGSIZE, kVMKSleep);
	} else
		return kmem_zonealloc(kmem_alloc_zones[zoneidx]);
}

void
kmem_free(void *ptr, size_t size)
{
	int zoneidx = zonenum(size);

	assert(size > 0);

	if (zoneidx == -1) {
		size_t realsize = PGROUNDUP(size);
		return vm_kfree(ptr, realsize / PGSIZE);
	} else
		return kmem_zonefree(kmem_alloc_zones[zoneidx], ptr);
}

#ifndef _KERNEL
int
main(int argc, char *argv[])
{
	void *two;

	kmem_init();

	printf("alloc 8/1: %p\n", kmem_zonealloc(&kmem_slab_8));
	two = kmem_zonealloc(&kmem_slab_8);
	printf("alloc 8/2: %p\n", two);
	printf("alloc 8/3: %p\n", kmem_zonealloc(&kmem_slab_8));

	kprintf("free 8/2\n");
	kmem_slabfree(&kmem_slab_8, two);

	printf("alloc 8/4 (should match 8/2): %p\n",
	    kmem_zonealloc(&kmem_slab_8));

	printf("alloc 1024/1: %p\n", kmem_zonealloc(&kmem_slab_1024));
	two = kmem_zonealloc(&kmem_slab_1024);
	printf("alloc 1024/2: %p\n", two);
	printf("alloc 1024/3: %p\n", kmem_zonealloc(&kmem_slab_1024));

	kprintf("free 1024/2\n");
	kmem_slabfree(&kmem_slab_1024, two);

	printf("alloc 1024/4 (should match 1024/2): %p\n",
	    kmem_zonealloc(&kmem_slab_1024));

	return 2;
}
#endif