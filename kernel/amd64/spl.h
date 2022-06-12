/*
 * SPL stands for System interrupt Priority Level.
 */
#ifndef SPL_H_
#define SPL_H_

static inline void splhigh()
{
	asm volatile("cli");
}

static inline void spl0()
{
	asm volatile("sti");
}

#endif /* SPL_H_ */
