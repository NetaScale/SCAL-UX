/*
 * SPL stands for System interrupt Priority Level.
 */

#ifndef SPL_H_
#define SPL_H_

#include <stdint.h>

typedef enum {
	kSPLHigh = 15, /* all interrupts blocked, including hardclock */
	kSPLHard = 3,  /* hard interrupts blocked */
	kSPLVM = kSPLHard,
	kSPLSoft = 2, /* soft interrupts blocked */
	kSPL0 = 0,    /* blocks none */
	_kSPLForceLong = INT64_MAX,
} spl_t;

/* get current SPL */
static inline spl_t
splget()
{
	spl_t spl;
	asm volatile("mov %%cr8, %0\n" : "=r"(spl));
	return spl; /* interrupt enable flag */
}

/* set SPL level to \p spl */
static inline spl_t
splx(spl_t spl)
{
	spl_t oldspl = splget();
	asm volatile("movq %0,%%cr8" ::"r"(spl) : "memory");
	return oldspl;
}

/* raise the SPL to \p spl if this is higher than current SPL */
static inline spl_t
splraise(spl_t spl)
{
	spl_t oldspl = splget();
	if (oldspl < spl)
		splx(spl);
	return oldspl;
}

/* raise SPL to high if this is higher than current */
static inline spl_t
splhigh()
{
	return splraise(kSPLHigh);
}

/* raise SPL to hard if this is higher than current */
static inline spl_t
splhard()
{
	return splraise(kSPLHard);
}

/* raise SPL to soft if this is higher than current */
static inline spl_t
splsoft()
{
	return splraise(kSPLSoft);
}

/* lower SPL to 0 */
static inline spl_t
spl0()
{
	return splx(kSPL0);
}

#endif /* SPL_H_ */
