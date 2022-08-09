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

typedef struct md_thread {
	md_intr_frame_t frame;
	uintptr_t fs;
} md_thread_t;

typedef struct md_cpu {
	uint64_t lapic_id;
	uint64_t lapic_tps; /* lapic timer ticks per second (divider 1) */
	struct tss *tss;
} md_cpu_t;

static inline struct cpu *
curcpu()
{
	struct cpu *val;
	asm volatile("mov %%gs:0, %0" : "=r"(val));
	return val;
}

#endif /* MACHDEP_H_ */
