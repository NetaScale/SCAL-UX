#ifndef POSIX_PROC_H_
#define POSIX_PROC_H_

#include "kern/process.h"
#include "vfs.h"

typedef struct posix_proc {
    process_t *proc; /* VXK process */

    file_t *files[64]; /* FD table */
} posix_proc_t;

#define CURPXPROC() CURCPU()->curthread->proc->pxproc

#endif /* POSIX_PROC_H_ */
