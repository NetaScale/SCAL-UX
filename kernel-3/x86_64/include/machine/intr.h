#ifndef INTR_H_
#define INTR_H_

#include <sys/types.h>
#include <stdint.h>

typedef struct md_intr_frame {
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
} __attribute__((packed)) md_intr_frame_t;

typedef void (*md_intr_handler_fn_t)(md_intr_frame_t *frame, void *arg);

#endif /* INTR_H_ */
