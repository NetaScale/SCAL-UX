#ifndef INTR_H_
#define INTR_H_

#include "spl.h"

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
	uint64_t  cs;
	uint64_t  rflags;
	uintptr_t rsp;
	uint64_t  ss;
} __attribute__((packed)) intr_frame_t;

typedef void (*intr_handler_fn_t)(intr_frame_t *frame, void *arg);

/**
 * This machine-dependent interface selects an appropriate vector for an
 * interrupt of a particular priority and then registers it with
 * md_intr_register. Particularly useful for cases where an interrupt source can
 * be routed to a specific vector and where vector number determines priority as
 * understood by hardware.
 *
 * @returns the vector assigned
 */
int md_intr_alloc(spl_t prio, intr_handler_fn_t handler, void *arg);

/** Register a handler for a particular interrupt vector. */
void md_intr_register(int vec, spl_t prio, intr_handler_fn_t handler,
    void *arg);

void md_intr_frame_trace(intr_frame_t *frame);

/**
 * Notify interrupt controller of end-of-interrupt. To be called by any
 * intr_handler_fn requiring to do so.
 */

void md_eoi();

#endif /* INTR_H_ */
