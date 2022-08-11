/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

/*
 * SPL stands for System Interrupt Priority Level.
 */

#ifndef SPL_H_
#define SPL_H_

#include <stdint.h>

typedef enum {
	kSPLHigh = 15, /* all interrupts blocked, including hardclock */
#if 0
	kSPLSched = kSPLHigh, /* scheduler; todo split hardclock out */
	kSPLHard = 3, /* hard interrupts blocked */
	kSPLVM = kSPLHard, /* virtual memory */
	kSPLBIO = kSPLHard, /* block I/O */
	kSPLSoft = 2, /* soft interrupts blocked */
#endif
	kSPL0 = 0,    /* blocks none */
	_kSPLForceLong = INT64_MAX,
} ipl_t;

/* get current IPL */
static inline ipl_t
splget()
{
	ipl_t ipl;
	asm volatile("mov %%cr8, %0\n" : "=r"(ipl));
	return ipl; /* interrupt enable flag */
}

/* set IPL level to \p ipl */
static inline ipl_t
splx(ipl_t ipl)
{
	ipl_t oldspl = splget();
	asm volatile("movq %0,%%cr8" ::"r"(ipl) : "memory");
	return oldspl;
}

/* raise the IPL to \p ipl if this is higher than current IPL */
static inline ipl_t
splraise(ipl_t ipl)
{
	ipl_t oldspl = splget();
	if (oldspl < ipl)
		splx(ipl);
	return oldspl;
}

#define splhigh() splraise(kSPLHigh)
#define splsoft() splraise(kSPLSoft)

/* lower IPL to 0 */
static inline ipl_t
spl0()
{
	return splx(kSPL0);
}

#endif /* SPL_H_ */
