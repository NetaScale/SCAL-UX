/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <sys/mman.h>

#include "abi-bits/errno.h"
#include "amd64/amd64.h"
#include "kern/vm.h"
#include "libkern/klib.h"
#include "posix/vfs.h"
#include "proc.h"
#include "sys.h"
#include "vm_posix.h"

int sys_exec(proc_t *proc, const char *path, const char *argp[],
    const char *envp[], intr_frame_t *frame);

int
posix_syscall(intr_frame_t *frame)
{
	proc_t   *proc = CURPSXPROC();
	thread_t *thread = CURCPU()->curthread;
	uintptr_t err = 0;

	assert(proc);

	lock(&thread->lock);
	thread->in_syscall = true;
#if 0
	if (thread->should_exit)
		goto cleanup;
	else
#endif
	unlock(&thread->lock);

#define ARG1 frame->rdi
#define ARG2 frame->rsi
#define ARG3 frame->rdx
#define ARG4 frame->r10
#define ARG5 frame->r8
#define ARG6 frame->r9

#define RET frame->rax
#define ERR frame->rdi

	switch (frame->rax) {
	case kPXSysDebug: {
		//kprintf("PXSYS_dbg: %s\n", (char *)ARG1);
		break;
	}

	case kPXSysExec: {
		const char *args[] = { "bash", "-l", NULL };
		const char *envs[] = {  NULL };
		assert(sys_exec(proc, "/usr/bin/bash", args, envs, frame) == 0);
		break;
	}

	case kPXSysMmap: {
		void *addr = (void *)ARG1 == NULL ? VADDR_MAX : (void *)ARG1;
		err = -vm_mmap(proc, &addr, ARG2, ARG3, ARG4, ARG5, ARG6);
		RET = (uintptr_t)addr;
		break;
	}

	case kPXSysOpen: {
		int r = sys_open(proc, (const char *)ARG1, ARG2);
		if (r < 0) {
			RET = -1;
			err = -r;
		} else {
			RET = r;
			err = 0;
		}
		break;
	}

	case kPXSysClose: {
		RET = sys_close(proc, ARG1, &err);
		break;
	}

	case kPXSysRead: {
		int r = sys_read(proc, ARG1, (void *)ARG2, ARG3);
		if (r < 0) {
			RET = -1;
			err = -r;
		} else {
			RET = r;
			err = 0;
		}
		break;
	}

	case kPXSysWrite: {
		int r = sys_write(proc, ARG1, (void *)ARG2, ARG3);
		if (r < 0) {
			RET = -1;
			err = -r;
		} else {
			RET = r;
			err = 0;
		}
		break;
	}

	case kPXSysSeek: {
		int r = sys_seek(proc, ARG1, ARG2, ARG3);
		if (r < 0) {
			RET = -1;
			err = -r;
		} else {
			RET = r;
			err = 0;
		}
		break;
	}

	case kPXSysPSelect: {
		RET = sys_pselect(proc, ARG1, (fd_set *)ARG2, (fd_set *)ARG3,
		    (fd_set *)ARG4, (struct timespec *)ARG5, (sigset_t *)ARG6,
		    &err);
		break;
	}

	case kPXSysIsATTY: {
		RET = sys_isatty(proc, ARG1, &err);
		break;
	}

	case kPXSysReadDir: {
		RET = sys_readdir(proc, ARG1, (void *)ARG2, ARG3,
		    (size_t *)ARG4, &err);
		break;
	}

	case kPXSysStat: {
		RET = sys_stat(proc, ARG1, (char *)ARG2, ARG3,
		    (struct stat *)ARG4, &err);
		break;
	}

	case kPXSysSetFSBase: {
		thread->pcb.fs = ARG1;
		wrmsr(kAMD64MSRFSBase, ARG1);
		RET = 0;
		break;
	}

	case kPXSysExecVE: {
		assert(sys_exec(proc, (char *)ARG1, (const char **)ARG2,
			   (const char **)ARG3, frame) == 0);
		break;
	}

	case kPXSysExit: {
		RET = sys_exit(proc, ARG1);
	}

	case kPXSysFork:
		RET = sys_fork(proc, &err);
		break;

	case kPXSysWaitPID:
		RET = sys_waitpid(proc, ARG1, (int *)ARG2, ARG3, &err);
		break;

	default:
		err = EOPNOTSUPP;
	}

	ERR = err;

cleanup:
	thread->in_syscall = false;
#if 0
	if (thread->should_exit) {
		spl_t spl = splhigh();
		thread->state = kExiting;
		CURCPU()->timeslice = 0;
		unlock(&thread->lock);
		splx(spl);
		asm("int $32");
	}
#endif
	unlock(&thread->lock);

	return 0;
}
