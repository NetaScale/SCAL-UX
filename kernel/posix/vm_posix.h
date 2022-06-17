#ifndef VM_POSIX_H_
#define VM_POSIX_H_

#include <sys/mman.h>

struct posix_proc;

int vm_mmap(struct posix_proc *proc, void **addr, size_t len, int prot,
    int flags, int fd, off_t offset);

#endif /* VM_POSIX_H_ */
