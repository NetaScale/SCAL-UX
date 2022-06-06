#ifndef VXKERN_H_
#define VXKERN_H_

#include <sys/nanoprintf.h>

#include <stdbool.h>

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

typedef volatile int spinlock_t;

void limterm_putc(int, void *);

void kmod_parsekern(void *addr);
void kmod_load(void *addr);

extern spinlock_t lock_msgbuf;

static inline void
lock(spinlock_t *lock)
{
	while (!__sync_bool_compare_and_swap(lock, 0, 1)) {
		while (*lock)
			__asm__("pause");
	}
}

static inline void
unlock(spinlock_t *lock)
{
	__sync_lock_release(lock);
}

/* needs lock/unlock */
#include "sys/nanoprintf.h"

#endif /* VXKERN_H_ */
