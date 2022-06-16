/*
 * The exec family of functions. Mlibc's runtime linker helpfully handles
 */

#include <sys/auxv.h>

#include <elf.h>
#include <errno.h>
#include <string.h>

#include "amd64.h"
#include "kern/liballoc.h"
#include "kern/process.h"
#include "kern/vm.h"
#include "vfs.h"
#include "pcb.h"

#define ELFMAG "\177ELF"

typedef struct exec_package {
	vaddr_t stack;	  /* bottom of stack */
	vaddr_t sp;	  /* initial stack pointer to execute with */
	vaddr_t entry;	  /* entry IP */
	vaddr_t phaddr;	  /* address of phdr */
	size_t phentsize; /* size of a phdr */
	size_t phnum;	  /* count of phdrs */
} exec_package_t;

static int
loadelf(const char *path, vaddr_t base, exec_package_t *pkg)
{
	vnode_t *vn;
	Elf64_Ehdr ehdr;
	Elf64_Phdr *phdrs;
	int r;

	r = vfs_lookup(root_vnode, &vn, path);
	if (r < 0) {
		kprintf("exec: failed to lookup %s (errno %d)\n", path, -r);
			return r;
	}

	r = vfs_read(vn, &ehdr, sizeof ehdr, 0);
	if (r < 0){
		kprintf("exec: failed to read %s (errno %d)\n", path, -r);
			return r;
	}

	if (memcmp(ehdr.e_ident, ELFMAG, 4) != 0){
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
	pkg->phaddr =0x0;

	for (int i = 0; i < ehdr.e_phnum; i++) {
		Elf64_Phdr *phdr = &phdrs[i];
		size_t pageoff;
		size_t size;
		vaddr_t segbase;

		if (phdr->p_type == PT_PHDR) {
			pkg->phaddr = base + phdr->p_vaddr;
			continue;
		} else if (phdr->p_type != PT_LOAD)
			continue;

		segbase = (vaddr_t)PGROUNDDOWN(phdr->p_vaddr);
		pageoff = phdr->p_vaddr - (uintptr_t)segbase;
		size = PGROUNDUP(pageoff + phdr->p_memsz);
		segbase += (uintptr_t)base;

		vm_allocate(kmap, NULL, &segbase, size, false);

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
	size_t narg = 0, nenv = 0;
	char *stackp = pkg->stack;
	uint64_t *stackpu64;

	for (const char **env = envp; *env; env++, nenv++) {
		stackp -= (strlen(*env) + 1);
		strcpy(stackp, *env);
		nenv++;
	}

	for (const char **arg = argp; *arg; arg++, narg++) {
		stackp -= (strlen(*arg) + 1);
		strcpy(stackp, *arg);
	}

	/* more convenient to work with this, and keep stackp for use later */
	stackpu64 = (uint64_t *)stackp;

	/* populate the auxv */
	#define AUXV(TAG, VALUE) *--stackpu64 = (uint64_t)VALUE; *--stackpu64 = TAG
	AUXV(0x0, 0x0);
	AUXV(AT_ENTRY, pkg->entry);
	AUXV(AT_PHDR, pkg->phaddr);
	AUXV(AT_PHENT, pkg->phentsize);
	AUXV(AT_PHNUM, pkg->phnum);

        *(--stackpu64) = 0;
        stackpu64 -= nenv;
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

/* todo: don't wipe the map, it makes it impossible to recover */
int
exec(const char *path, const char *argp[], const char *envp[], intr_frame_t *frame)
{
	int r;
	exec_package_t pkg, rtldpkg;

	/* assume it's not PIE */
	r = loadelf(path, (vaddr_t)0x0, &pkg);
	if (r < 0)
		return r;

	r = loadelf("/ld.so", (vaddr_t)0x40000000, &rtldpkg);
	if (r < 0)
		return r;

	pkg.stack = VADDR_MAX;
	assert(vm_allocate(kmap, NULL, &pkg.stack, 4096 * 8, false) == 0);
	pkg.stack += 4096 * 8;
	assert(copyargs(&pkg, argp, envp) == 0);

	frame->rip = (uint64_t)rtldpkg.entry;
	frame->rsp = (uint64_t)pkg.sp;
	CURCPU()->curthread->pcb.frame.rip = (uint64_t)rtldpkg.entry;
	CURCPU()->curthread->pcb.frame.rsp = (uint64_t)pkg.sp;

	return 0;
}