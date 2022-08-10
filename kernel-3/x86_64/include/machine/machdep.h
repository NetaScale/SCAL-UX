/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#ifndef MACHDEP_H_
#define MACHDEP_H_

#include <machine/intr.h>

#include <stdbool.h>

struct thread;

typedef struct md_thread {
	md_intr_frame_t frame;
	uintptr_t	fs;
} md_thread_t;

typedef struct md_cpu {
	uint64_t    lapic_id;
	uint64_t    lapic_tps; /* lapic timer ticks per second (divider 1) */
	struct tss *tss;
	struct thread *old;
} md_cpu_t;

static inline struct cpu *
curcpu()
{
	struct cpu *val;
	asm volatile("mov %%gs:0, %0" : "=r"(val));
	return val;
}

static inline uintptr_t
md_intr_disable()
{
	uintptr_t flags;
	asm volatile("pushf\n\t"
		     "pop %0"
		     : "=rm"(flags)
		     : /* epsilon */
		     : "memory");
	return flags & (1 << 9);
}

static inline void
md_intr_x(bool en)
{
	if (en)
		asm volatile("pushf\n\t"
			     "pop %%r10\n\t"
			     "bts $9, %%r10\n\t"
			     "push %%r10\n\t"
			     "popf"
			     : /* epsilon */
			     : /* epsilon */
			     : "r10", "memory");
}

void md_switch(struct thread *from, struct thread *to);

#endif /* MACHDEP_H_ */
