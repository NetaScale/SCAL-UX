
#include "kern/kern.h"
#include "pcb.h"
#include "sys.h"

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
		for (;;)
			asm("hlt");
	}
	}
}