/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#ifndef KLIB_H_
#define KLIB_H_

#include <machine/spl.h>
#include <kern/lock.h>

#include <string.h>

#include "nanoprintf.h"

#ifdef ARCH_HOSTED
#include <stdio.h>
#include <stdlib.h>

static inline void
limterm_putc(int ch, void *ctx)
{
	putc(ch, stdout);
}

#define kmalloc malloc
#else
void limterm_putc(int ch, void *ctx);
#endif

extern spinlock_t lock_msgbuf;

#define kprintf(...) npf_pprintf(limterm_putc, NULL, __VA_ARGS__)
#define kvpprintf(...) npf_vpprintf(limterm_putc, NULL, __VA_ARGS__)
#define ksnprintf(...) npf_snprintf(__VA_ARGS__)
#define assert(...)                                                    \
	{                                                              \
		if (!(__VA_ARGS__))                                    \
			fatal("assertion failed: " #__VA_ARGS__ "\n"); \
	}
#define fatal(...)                      \
	{                               \
		kprintf(__VA_ARGS__);   \
		while (1) {             \
			__asm__("hlt"); \
		}                       \
	}
#define unimplemented(...) fatal("%s: unimplemented\n", __PRETTY_FUNCTION__)

#endif /* KLIB_H_ */
