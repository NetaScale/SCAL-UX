
#include "kern/kern.h"
#include "pcb.h"
#include "sys.h"

int
exec(const char *path, const char *argp[], const char *envp[]);

int
posix_syscall(intr_frame_t *frame)
{
	switch (frame->rax) {
	case PXSYS_dbg: {
		kprintf("PXSYS_debug(%s)\n", frame->rdi);
		break;
	}

	case PXSYS_exec: {
		kprintf("PXSYS_exec(%s)\n", frame->rdi);
		const char *args[] = { "init", "no"};
		const char *envs[] = { "VAR=42" };
		assert(exec("/init", args, envs) == 0);
		for (;;)
			asm("hlt");
	}
	}
}