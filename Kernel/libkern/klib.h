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
#include <machine/intr.h>

#include <kern/liballoc.h>
#include <kern/lock.h>
#include <string.h>

#include "kern/task.h"
#include "nanoprintf.h"

#define ELEMENTSOF(ARR) (sizeof(ARR) / sizeof(ARR[0]))

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
		kprintf("on CPU %lu, PID %d, thread %p:\n", CURCPU()->num, CURTASK()->pid, CURTHREAD()); \
		kprintf(__VA_ARGS__);   \
		while (1) {             \
			__asm__("cli"); \
		}                       \
	}
#define unimplemented(...) fatal("%s: unimplemented\n", __PRETTY_FUNCTION__)

#if 0
extern void    *(kmalloc)(size_t);
extern void    *(krealloc)(void *, size_t);
extern void    *(kcalloc)(size_t, size_t);
extern void     (kfree)(void *);
#endif

/* assert SPL less than or equal to \p spl */
static inline void
splassertle(spl_t spl)
{
	if (splget() > spl)
		fatal("SPL_NOT_LESS_OR_EQUAL %lx\n", spl);
}

/* assert SPL less than or equal to \p spl */
static inline void
splassertge(spl_t spl)
{
	if (splget() > spl)
		fatal("SPL_NOT_GREATER_OR_EQUAL %lx\n", spl);
}

#endif /* KLIB_H_ */
