#ifndef VXKERN_H_
#define VXKERN_H_

#include "nanoprintf.h"

#define kprintf(...) npf_pprintf(limterm_putc, NULL, __VA_ARGS__)
#define fatal(...)                      \
	{                               \
		kprintf(__VA_ARGS__);   \
		while (1) {             \
			__asm__("hlt"); \
		}                       \
	}

void limterm_putc(int, void *);

#endif /* VXKERN_H_ */
