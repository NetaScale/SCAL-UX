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
 * @file vm.h
 * @brief Interface to the kernel's virtual memory manager.
 *
 * \addtogroup Kernel
 * @{
 * \defgroup VMM Virtual Memory Management
 * The virtual memory manager manages virtual memory.
 * @}
 */

#ifndef VM_H_
#define VM_H_

#include <kern/types.h>

#define PGSIZE 4096
#define VADDR_MAX (vaddr_t) UINT64_MAX

#define ROUNDUP(addr, align) ((((uintptr_t)addr) + align - 1) & ~(align - 1))
#define ROUNDDOWN(addr, align) ((((uintptr_t)addr)) & ~(align - 1))

#define PTRROUNDUP(addr) ROUNDUP(addr, (sizeof(uintptr_t)))
/** Round a value up to pointer alignment. */
#define PTRROUNDDOWN(addr) ROUNDDOWN(addr, (sizeof(uintptr_t)))

#define PGROUNDUP(addr) ROUNDUP(addr, PGSIZE)
#define PGROUNDDOWN(addr) ROUNDDOWN(addr, PGSIZE)

/*!
 * @name Kernel memory
 * @{
 */

/*! Flags that may be passed to vm_kalloc(). */
enum vm_kalloc_flags {
        /*! immediately return NULL if no free pages currently */
        kVMKNoSleep = 0,
        /*! infallible; sleepwait for a page if no pages currently available */
        kVMKSleep = 1,
};

/** Set up the kernel memory subsystem. */
void vm_kernel_init();

/*!
 * Allocate pages of kernel heap.
 *
 * @param flags see vm_kalloc_flags
 */
vaddr_t vm_kalloc(size_t npages, enum vm_kalloc_flags flags);

/*!
 * Free pages of kernel heap.
 */
void vm_kfree(vaddr_t addr, size_t pages);

/** @} */

#endif /* VM_H_ */
