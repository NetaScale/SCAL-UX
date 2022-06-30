/*
 * miscellaneous definitions for in-kernel
 */

#ifndef KERN_H_
#define KERN_H_

#include <stdbool.h>
#include <stdint.h>

#include "amd64/spl.h"
#include "klock.h"

#define kprintf(...) npf_pprintf(limterm_putc, NULL, __VA_ARGS__)
#define kvpprintf(...) npf_vpprintf(limterm_putc, NULL, __VA_ARGS__)
#define ksnprintf(...) npf_snprintf(__VA_ARGS__)
#define assert(...)                          \
	{                                    \
		if (!(__VA_ARGS__))          \
			fatal("assertion failed: " #__VA_ARGS__ "\n"); \
	}
#define fatal(...)                      \
	{                               \
		kprintf(__VA_ARGS__);   \
		while (1) {             \
			__asm__("hlt"); \
		}                       \
	}
#define unimplemented(...) fatal( "%s: unimplemented\n", __PRETTY_FUNCTION__)

void limterm_putc(int, void *);

void kmod_parsekern(void *addr);
void kmod_load(void *addr);

extern spinlock_t lock_msgbuf;

/* needs lock/unlock, lock_msgbuf... */
#include "nanoprintf.h"

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


#endif /* KERN_H_ */
