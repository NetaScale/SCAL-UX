/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

/**
 * @file klib_libc.h
 * @brief Compatibility header exposing libc functions for use e.g. by ObjFW.
 */

#ifndef KLIB_LIBC_H_
#define KLIB_LIBC_H_

#include <kern/kmem.h>
#include <libkern/klib.h>
#include <kern/liballoc.h>

#include <string.h>

#define fflush(...)
#define printf(...) kprintf(__VA_ARGS__)
#define fprintf(file, ...) kprintf(__VA_ARGS__)
#define vfprintf(file, ...) kvpprintf(__VA_ARGS__)

#define abort() fatal("abort!")

#define malloc liballoc_kmalloc
#define calloc liballoc_kcalloc
#define free liballoc_kfree
#define realloc liballoc_krealloc

void *klib_libc_gencalloc(size_t nmemb, size_t size);

#endif /* KLIB_LIBC_H_ */
