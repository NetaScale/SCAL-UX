
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

	assert(proc);

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
		CURCPU()->curthread->pcb.fs = ARG1;
		wrmsr(kAMD64MSRFSBase, ARG1);
		RET = 0;
		break;
	}

	}

	CURCPU()->curthread->pcb.frame.rax = RET;
	CURCPU()->curthread->pcb.frame.rdi = ERR;

	return 0;
}
