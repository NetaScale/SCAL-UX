#ifndef SYS_H_
#define SYS_H_

/* number in rax, arg1 rdi, arg2 rsi, arg3 rdx, arg4 r10, arg5 r8, arg6 r9 */

#define PXSYS_dbg 1 /* const char *text */
#define PXSYS_exec 2 /* const char *path */
#define PXSYS_mmap 3 /* void *addr, size_t length, int prot, int flags, int fd, off_t off */

#endif /* SYS_H_ */
