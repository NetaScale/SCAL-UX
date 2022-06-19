#ifndef POSIX_PROC_H_
#define POSIX_PROC_H_

#include "kern/process.h"
#include "vfs.h"

#define CURPXPROC() CURCPU()->curthread->proc->pxproc

typedef struct posix_proc {
	process_t *proc; /* VXK process */

	file_t *files[64]; /* FD table */
} posix_proc_t;

int sys_exit(posix_proc_t *proc, int code);

#endif /* POSIX_PROC_H_ */
