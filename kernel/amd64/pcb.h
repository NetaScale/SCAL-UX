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

typedef struct {
	uint32_t reserved;
	uint64_t rsp0;
	uint64_t rsp1;
	uint64_t rsp2;
	uint64_t reserved1;
	uint64_t ist1;
	uint64_t ist2;
	uint64_t ist3;
	uint64_t ist4;
	uint64_t ist5;
	uint64_t ist6;
	uint64_t ist7;
	uint64_t reserved2;
	uint32_t iopb;
} __attribute__((packed)) tss_t;

typedef struct pcb {
	intr_frame_t frame;
	uintptr_t fs;
} pcb_t;

#endif /* PCB_H_ */
