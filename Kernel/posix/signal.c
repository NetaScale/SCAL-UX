/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */
 
#include <signal.h>

#include "kern/task.h"
#include "libkern/klib.h"

typedef struct sigframe {
	siginfo_t  siginfo;
	ucontext_t ctx;
} sigframe_t;

int
process_signal(thread_t *thr)
{
	void	     *sp = (void *)thr->pcb.frame.rsp;
	sigframe_t *frame;

	sp -= 128; /* clear redzone */
	sp -= sizeof(sigframe_t);
	frame = sp;

	/*
	 * signal processing function looks like:
	 * void __scalux_handle_signal(siginfo_t *siginfo, ucontext_t *ucontext,
	 *     void *sigframe)
	 *
	 * mcontext_t's __gregs member is identical to an intr_frame_t.
	 */
	memcpy(frame->ctx.uc_mcontext.__gregs, &thr->pcb.frame,
	    sizeof(thr->pcb.frame));

	thr->pcb.frame.rdi = (uintptr_t)&frame->siginfo;
	thr->pcb.frame.rsi = (uintptr_t)&frame->ctx;
	thr->pcb.frame.rdx = (uintptr_t)frame;
	thr->pcb.frame.rsp = (uintptr_t)sp;

        /* ... todo */
}
