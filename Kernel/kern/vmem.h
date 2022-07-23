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
 * @file vmem.h
 * @brief Public interface to the VMem resource allocator. See vmem.c for
 * detailed description of VMem.
 */

#ifndef VMEM_H_
#define VMEM_H_

#include <stddef.h>
#include <stdint.h>

#ifndef _KERNEL
typedef int spl_t;
#else
#include <machine/spl.h>
#endif

typedef uintptr_t   vmem_addr_t;
typedef size_t	    vmem_size_t;
typedef struct vmem vmem_t;

typedef enum vmem_flag {
	kVMemSleep = 0x0,
	kVMemNoSleep = 0x1,
	kVMemExact = 0x2,
	/** @private */
	kVMemBootstrap = 0x4,
} vmem_flag_t;

// clang-format off
typedef int (*vmem_alloc_t)(vmem_t *vmem, vmem_size_t size,
    vmem_flag_t flags, vmem_addr_t *out);
typedef void (*vmem_free_t)(vmem_t *vmem, vmem_addr_t addr, vmem_size_t size);
// clang-format on

/** Create a new VMem arena. */
vmem_t *vmem_init(vmem_t *vmem, const char *name, vmem_addr_t base,
    vmem_size_t size, vmem_size_t quantum, vmem_alloc_t allocfn,
    vmem_free_t freefn, vmem_t *source, size_t qcache_max, vmem_flag_t flags,
    spl_t spl);
/** Destroy a VMem arena. (Does not free it; that must be done manually.) */
void vmem_destroy(vmem_t *vmem);

int vmem_xalloc(vmem_t *vmem, vmem_size_t size, vmem_size_t align,
    vmem_size_t phase, vmem_size_t nocross, vmem_addr_t min, vmem_addr_t max,
    vmem_flag_t flags, vmem_addr_t *out);

int vmem_xfree(vmem_t *vmem, vmem_addr_t addr, vmem_size_t size);

void vmem_dump(const vmem_t *vmem);

#endif /* VMEM_H_ */
