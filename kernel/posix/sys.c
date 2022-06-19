
#include <sys/mman.h>

#include "amd64.h"
#include "kern/kern.h"
#include "kern/vm.h"
#include "pcb.h"
#include "posix_proc.h"
#include "sys.h"
#include "vm_posix.h"

int exec(const char *path, const char *argp[], const char *envp[],
    intr_frame_t *frame);

int
posix_syscall(intr_frame_t *frame)
{
	posix_proc_t *proc = CURPXPROC();
	thread_t *thread = CURCPU()->curthread;
	uintptr_t arg1 = frame->rdi;

	assert(proc);

	lock(&thread->lock);
	thread->in_syscall = true;
	if (thread->should_exit)
		goto cleanup;
	else
		unlock(&thread->lock);

#define ARG1 arg1
#define ARG2 frame->rsi
#define ARG3 frame->rdx
#define ARG4 frame->r10
#define ARG5 frame->r8
#define ARG6 frame->r9

#define RET frame->rax
#define ERR frame->rdi

	ERR = 0;

	switch (frame->rax) {
	case kPXSysDebug: {
		kprintf("PXSYS_dbg: %s\n", (char *)ARG1);
		break;
	}

	case kPXSysExec: {
		kprintf("PXSYS_exec: %s\n", (char *)frame->rdi);
		const char *args[] = { "init", "no", NULL };
		const char *envs[] = { "VAR=42", NULL };
		assert(exec("/init", args, envs, frame) == 0);
		break;
	}

	case kPXSysMmap: {
		void *addr = (void *)ARG1 == NULL ? VADDR_MAX : (void *)ARG1;
		ERR = -vm_mmap(proc, &addr, ARG2, ARG3, ARG4, ARG5, ARG6);
		RET = (uintptr_t)addr;
		break;
	}

	case kPXSysOpen: {
		int r = sys_open(proc, (const char *)ARG1, ARG2);
		if (r < 0) {
			RET = -1;
			ERR = -r;
		} else {
			RET = r;
			ERR = 0;
		}
		break;
	}

	case kPXSysClose: {
		RET = sys_close(proc, ARG1, &ERR);
		break;
	}

	case kPXSysRead: {
		int r = sys_read(proc, ARG1, ARG2, ARG3);
		if (r < 0) {
			RET = -1;
			ERR = -r;
		} else {
			RET = r;
			ERR = 0;
		}
		break;
	}

	case kPXSysSeek: {
		int r = sys_seek(proc, ARG1, ARG2, ARG3);
		if (r < 0) {
			RET = -1;
			ERR = -r;
		} else {
			RET = r;
			ERR = 0;
		}
		break;
	}

	case kPXSysSetFSBase: {
		thread->pcb.fs = ARG1;
		wrmsr(kAMD64MSRFSBase, ARG1);
		RET = 0;
		break;
	}

	case kPXSysExit: {
		RET = sys_exit(proc, ARG1);
	}
	}

	thread->pcb.frame.rax = RET;
	thread->pcb.frame.rdi = ERR;

cleanup:
	thread->in_syscall = false;
	if (thread->should_exit) {
		spl_t spl = splhigh();
		thread->state = kExiting;
		CURCPU()->timeslice = 0;
		unlock(&thread->lock);
		splx(spl);
		asm("int $32");
	}
	unlock(&thread->lock);

	return 0;
}
