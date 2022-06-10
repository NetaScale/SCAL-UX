#ifndef VXKERN_H_
#define VXKERN_H_

#include <sys/nanoprintf.h>
#include <vxk/klock.h>

#include <stdbool.h>
#include <stdint.h>

#define kprintf(...) npf_pprintf(limterm_putc, NULL, __VA_ARGS__)
#define kvpprintf(...) npf_vpprintf(limterm_putc, NULL, __VA_ARGS__)
#define assert(...)                          \
	{                                    \
		if (!(__VA_ARGS__))          \
			fatal(#__VA_ARGS__); \
	}
#define fatal(...)                      \
	{                               \
		kprintf(__VA_ARGS__);   \
		while (1) {             \
			__asm__("hlt"); \
		}                       \
	}

void limterm_putc(int, void *);

void kmod_parsekern(void *addr);
void kmod_load(void *addr);

extern spinlock_t lock_msgbuf;

/* needs lock/unlock */
#include "sys/nanoprintf.h"

#endif /* VXKERN_H_ */
