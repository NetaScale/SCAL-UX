/*
 * SPL stands for System interrupt Priority Level.
 */
#ifndef SPL_H_
#define SPL_H_

#include <stdint.h>

typedef int spl_t;

static inline spl_t
splget()
{
	uint64_t flags;
	asm volatile("pushf\n"
		     "pop %0"
		     : "=rm"(flags)
		     :
		     : "memory");
	return flags & (1 << 9); /* interrupt enable flag */
}

static inline void
splx(spl_t spl)
{
	if (spl == 0)
		asm volatile("cli");
	else
		asm volatile("sti");
}

static inline spl_t
splhigh()
{
	spl_t spl = splget();
	asm volatile("cli");
	return spl;
}

static inline spl_t
spl0()
{
	spl_t spl = splget();
	asm volatile("sti");
	return splget();
}

#endif /* SPL_H_ */
