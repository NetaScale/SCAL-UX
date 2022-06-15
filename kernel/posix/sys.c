
#include "kern/kern.h"
#include "pcb.h"
#include "sys.h"

int
exec(const char *path, const char *argp[], const char *envp[], intr_frame_t *frame);

int
posix_syscall(intr_frame_t *frame)
{
	switch (frame->rax) {
	case PXSYS_dbg: {
		kprintf("PXSYS_dbg: %s\n", frame->rdi);
		break;
	}

	case PXSYS_exec: {
		kprintf("PXSYS_exec: %s\n", frame->rdi);
		const char *args[] = { "init", "no", NULL};
		const char *envs[] = { "VAR=42", NULL };
		assert(exec("/init", args, envs, frame) == 0);
		break;
	}

	case PXSYS_mmap:
		kprintf("PXSYS_mmap");
		for (;;) asm ("hlt");
	}
}