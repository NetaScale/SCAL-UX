/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

/*
 * The exec family of functions.
 */

#include <sys/auxv.h>

#include <elf.h>
#include <errno.h>
#include <string.h>

#include "kern/liballoc.h"
#include "kern/vm.h"
#include "libkern/klib.h"
#include "machine/pcb.h"
#include "proc.h"
#include "vfs.h"

#define ELFMAG "\177ELF"

typedef struct exec_package {
	vm_map_t *map;	     /* map to load into */
	vaddr_t	  stack;     /* bottom of stack */
	vaddr_t	  sp;	     /* initial stack pointer to execute with */
	vaddr_t	  entry;     /* entry IP */
	vaddr_t	  phaddr;    /* address of phdr */
	size_t	  phentsize; /* size of a phdr */
	size_t	  phnum;     /* count of phdrs */
} exec_package_t;

static int
loadelf(const char *path, vaddr_t base, exec_package_t *pkg)
{
	vnode_t	*vn;
	Elf64_Ehdr  ehdr;
	Elf64_Phdr *phdrs;
	int	    r;

	r = vfs_lookup(root_vnode, &vn, path, 0, NULL);
	if (r < 0) {
		kprintf("exec: failed to lookup %s (errno %d)\n", path, -r);
		return r;
	}

	r = vfs_read(vn, &ehdr, sizeof ehdr, 0);
	if (r < 0) {
		kprintf("exec: failed to read %s (errno %d)\n", path, -r);
		return r;
	}

	if (memcmp(ehdr.e_ident, ELFMAG, 4) != 0) {
		kprintf("exec: bad e_ident in %s\n", path);
		return -ENOEXEC;
	}

	phdrs = kmalloc(ehdr.e_phnum * ehdr.e_phentsize);
	if (!phdrs)
		return -ENOMEM;

	r = vfs_read(vn, phdrs, ehdr.e_phnum * ehdr.e_phentsize, ehdr.e_phoff);
	if (r < 0)
		return r;

	pkg->entry = base + ehdr.e_entry;
	pkg->phentsize = ehdr.e_phentsize;
	pkg->phnum = ehdr.e_phnum;
	pkg->phaddr = 0x0;

	for (int i = 0; i < ehdr.e_phnum; i++) {
		Elf64_Phdr *phdr = &phdrs[i];
		size_t	    pageoff;
		size_t	    size;
		vaddr_t	    segbase;

		if (phdr->p_type == PT_PHDR) {
			pkg->phaddr = base + phdr->p_vaddr;
			continue;
		} else if (phdr->p_type != PT_LOAD)
			continue;

		segbase = (vaddr_t)PGROUNDDOWN(phdr->p_vaddr);
		pageoff = phdr->p_vaddr - (uintptr_t)segbase;
		size = PGROUNDUP(pageoff + phdr->p_memsz);
		segbase += (uintptr_t)base;

		assert(vm_allocate(pkg->map, NULL, &segbase, size) == 0);

		r = vfs_read(vn, segbase + pageoff, phdr->p_filesz,
		    phdr->p_offset);
		if (r < 0)
			return r; /* TODO: this won't work anymore */
	}

	return 0;
}

static int
copyargs(exec_package_t *pkg, const char *argp[], const char *envp[])
{
	size_t	  narg = 0, nenv = 0;
	char     *stackp = pkg->stack;
	uint64_t *stackpu64;

	for (const char **env = envp; *env; env++, nenv++) {
		stackp -= (strlen(*env) + 1);
		strcpy(stackp, *env);
	}

	for (const char **arg = argp; *arg; arg++, narg++) {
		stackp -= (strlen(*arg) + 1);
		strcpy(stackp, *arg);
	}

	/* align to 16 bytes */
	stackpu64 = (size_t *)(stackp - ((uintptr_t)stackp & 0xf));
	/* account for args/env */
	if ((narg + nenv + 3) % 2)
		--stackpu64;

/* populate the auxv */
#define AUXV(TAG, VALUE)                \
	*--stackpu64 = (uint64_t)VALUE; \
	*--stackpu64 = TAG
	AUXV(0x0, 0x0);
	AUXV(AT_ENTRY, pkg->entry);
	AUXV(AT_PHDR, pkg->phaddr);
	AUXV(AT_PHENT, pkg->phentsize);
	AUXV(AT_PHNUM, pkg->phnum);

	*(--stackpu64) = 0;
	stackpu64 -= nenv;
	stackp = pkg->stack;

	for (int i = 0; i < nenv; i++) {
		stackp -= strlen(envp[i]) + 1;
		stackpu64[i] = (uint64_t)stackp;
	}

	*(--stackpu64) = 0;
	stackpu64 -= narg;
	for (int i = 0; i < narg; i++) {
		stackp -= strlen(argp[i]) + 1;
		stackpu64[i] = (uint64_t)stackp;
	}

	*(--stackpu64) = narg;

	pkg->sp = stackpu64;

	return 0;
}

static int
copyout_strv(const char *user_strv[], char ***out)
{
	int    cnt = 0;
	char **strv;

	while (true) {
		if (user_strv[cnt++] == NULL)
			break;
	}

	strv = kmalloc((sizeof *user_strv) * cnt);
	memcpy(strv, user_strv, (sizeof *user_strv) * cnt);

	cnt = 0;
	while (strv[cnt] != NULL) {
		strv[cnt] = strdup(strv[cnt]);
		cnt++;
	}

	*out = strv;

	return 0;
}

static void
strv_free(char **strv)
{
	if (*strv == NULL)
		return;

	for (char **ptr = strv; *ptr != NULL; ptr++)
		kfree(*ptr);
	kfree(strv);
}

int
sys_exec(proc_t *proc, const char *u_path, const char *u_argp[],
    const char *u_envp[], intr_frame_t *frame)
{
	int	       r = 0;
	exec_package_t pkg, rtldpkg;
	vaddr_t	       stack = VADDR_MAX;
	thread_t	 *thread = CURTHREAD();
	char	     *path = NULL, **argp = NULL, **envp = NULL;
	vm_map_t	 *oldmap = proc->task->map;

	/* TODO: end all other threads of the process.... */

#if DEBUG_SYSCALLS == 1
	kprintf("SYS_EXEC(%s)\n", u_path);
#endif

	assert(proc->task->map != &kmap);
	pkg.map = rtldpkg.map = vm_map_new();
	assert(pkg.map != NULL);

	path = strdup(u_path);
	assert(copyout_strv(u_argp, &argp) == 0);
	assert(copyout_strv(u_envp, &envp) == 0);

	proc->task->map = pkg.map;
	vm_activate(pkg.map);

	/* assume it's not PIE */
	r = loadelf(path, (vaddr_t)0x0, &pkg);
	if (r < 0)
		goto fail;

	r = loadelf("/usr/lib/ld.so", (vaddr_t)0x40000000, &rtldpkg);
	if (r < 0)
		goto fail;

	pkg.stack = VADDR_MAX;
	assert(vm_allocate(pkg.map, NULL, &stack, USER_STACK_SIZE) == 0);
	stack += USER_STACK_SIZE;
	pkg.stack = stack;
	assert(copyargs(&pkg, argp, envp) == 0);

	vm_map_release(oldmap);
	thread->stack = stack;

	frame->rip = (uint64_t)rtldpkg.entry;
	frame->rsp = (uint64_t)pkg.sp;

	r = 0;
	goto succ;

fail:
	vm_activate(oldmap);
	proc->task->map = oldmap;

succ:
	kfree(path);
	strv_free(argp);
	strv_free(envp);

	return r;
}
