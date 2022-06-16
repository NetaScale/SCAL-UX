
#include <sys/mman.h>

#include "kern/kern.h"
#include "vm_posix.h"
#include "pcb.h"
#include "sys.h"

int
exec(const char *path, const char *argp[], const char *envp[], intr_frame_t *frame);

int
posix_syscall(intr_frame_t *frame)
{
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
		kprintf("PXSYS_dbg: %s\n", frame->rdi);
		break;
	}

	case kPXSysExec: {
		kprintf("PXSYS_exec: %s\n", frame->rdi);
		const char *args[] = { "init", "no", NULL};
		const char *envs[] = { "VAR=42", NULL };
		assert(exec("/init", args, envs, frame) == 0);
		break;
	}

	case kPXSysMmap: {
		void * addr = (void*)ARG1;
		ERR = -vm_mmap(&addr, ARG2, ARG3, ARG4, ARG5, ARG6);
		RET = (uintptr_t)addr;
		break;
	}
	}

	return 0;
}