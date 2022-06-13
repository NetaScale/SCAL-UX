/*
 * SPL stands for System interrupt Priority Level.
 */
#ifndef SPL_H_
#define SPL_H_

#include <stdint.h>

#include "intr.h"
#include "kern/kern.h"

/*
 * The principle: interrupts from 32 to 48 for soft. from 48 to 80 for hard.
 * - interrupts from 0 are blockable only with kSPLHigh (except NMIs)
 * - interrupts < 32 blocked with kSPLSoft. This is just a logical level at the
 * moment. It's checked for to decide whether to invoke scheduling after
 * completion of an interrupt handler.
 * - interrupts < 48  blocked with kSPLHard
 * - no interrupts blocked with kSPL0
 *
 * The tick interrupt (32) handler invokes the scheduler if needed by
 * raising int 81.
 */

typedef enum {
	kSPLHigh = 0xff, /* all interrupts blocked */
	kSPLHard = 0x3,	 /* hard interrupts blocked */
	kSPLSoft = 0x2,	 /* soft interrupts blocked */
	kSPL0 = 0x0,	 /* blocks none */
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
