/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#ifndef PCB_H_
#define PCB_H_

#include <stdint.h>

typedef struct intr_frame {
	uint64_t rax;
	uint64_t rbx;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rdi;
	uint64_t rsi;
	uint64_t r8;
	uint64_t r9;
	uint64_t r10;
	uint64_t r11;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t rbp;
	uint64_t code; /* may be fake */

	uintptr_t rip;
	uint64_t cs;
	uint64_t rflags;
	uintptr_t rsp;
	uint64_t ss;
} __attribute__((packed)) intr_frame_t;

typedef struct arch_pcb {
	intr_frame_t frame;
	uintptr_t fs;
} arch_pcb_t;

typedef struct arch_cpu {
	uint64_t lapic_id;
	uint64_t lapic_tps; /* lapic timer ticks per second (divider 1) */
	struct tss *tss;
} arch_cpu_t;

static inline struct cpu *
CURCPU()
{
	struct cpu *val;
	asm volatile("mov %%gs:0, %0" : "=r"(val));
	return val;
}

#endif /* PCB_H_ */
