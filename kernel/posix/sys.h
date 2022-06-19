#ifndef SYS_H_
#define SYS_H_

#include <stdint.h>

enum {
	kPXSysNull = 0,
	kPXSysDebug,
	kPXSysExec,
	kPXSysMmap,
	kPXSysOpen,
	kPXSysClose,
	kPXSysRead,
	kPXSysSeek,
	kPXSysSetFSBase,
	kPXSysExit,
};

/*
 * number in rax, arg1 rdi, arg2 rsi, arg3 rdx, arg4 r10, arg5 r8, arg6 r9
 * result out rax, error code out rdi
 */

static inline uintptr_t
syscall1(uintptr_t num, uintptr_t arg1, uintptr_t *errp)
{
	uintptr_t ret, err;
	asm volatile("int $0x80"
		     : "=a"(ret), "=D"(err)
		     : "a"(num), "D"(arg1)
		     : "memory");
	if (errp)
		*errp = err;
	return ret;
}

static inline uintptr_t
syscall2(uintptr_t num, uintptr_t arg1, uintptr_t arg2, uintptr_t *errp)
{
	uintptr_t ret, err;

	asm volatile("int $0x80"
		     : "=a"(ret), "=D"(err)
		     : "a"(num), "D"(arg1), "S"(arg2)
		     : "memory");

	if (errp)
		*errp = err;

	return ret;
}

static inline uintptr_t
syscall3(intptr_t num, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
    uintptr_t *errp)
{
	uintptr_t ret, err;

	asm volatile("int $0x80"
		     : "=a"(ret), "=D"(err)
		     : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3)
		     : "memory");

	if (errp)
		*errp = err;

	return ret;
}

static inline uintptr_t
syscall6(uintptr_t num, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
    uintptr_t arg4, uintptr_t arg5, uintptr_t arg6, uintptr_t *errp)
{
	register uintptr_t r10 asm("r10") = arg4, r8 asm("r8") = arg5,
			       r9 asm("r9") = arg6;
	uintptr_t ret, err;

	asm volatile("int $0x80"
		     : "=a"(ret), "=D"(err)
		     : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10),
		     "r"(r8), "r"(r9)
		     : "memory");

	if (errp)
		*errp = err;

	return ret;
}

#endif /* SYS_H_ */
