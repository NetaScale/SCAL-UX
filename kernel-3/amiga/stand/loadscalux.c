/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <sys/param.h>

#include <assert.h>
#include <clib/exec_protos.h>
#include <elf.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROUNDUP(addr, align) ((((uintptr_t)addr) + align - 1) & ~(align - 1))
#define PGSIZE 0x1000

#define err(...)                     \
	{                            \
		perror(__VA_ARGS__); \
		exit(EXIT_FAILURE);  \
	}

static void
setup_dttr()
{
	void    *sstack;
	uint32_t dtt = 0;

#define MASKBITS(BITS) ((1 << BITS) - 1)
#define TTR_BASE 24 /* 8 bits base address */
#define TTR_MASK 16 /* mask, 8 bits */
#define TTR_EN 15   /* enable, 1 bit */
#define TTR_S 13    /* 2 bits, set to 3 for ignore supervisor/user mode */
#define TTR_CM 5    /* cache mode, 3 = noncacheable */
	dtt |= (unsigned)0 << TTR_BASE; /* want high bit to be zero */
	dtt |= MASKBITS(7) << TTR_MASK; /* ignore all but high bit */
	dtt |= (unsigned)1 << TTR_EN;
	dtt |= (unsigned)3 << TTR_S;
	dtt |= (unsigned)2 << TTR_CM;

	sstack = SuperState();
	asm("movec %0, %%dtt0" ::"d"(dtt));
	asm("movec %0, %%itt0" ::"d"(dtt));
	UserState(sstack);
}

int
main(int argc, char *argv[])
{
	struct MemHeader *mh;
	FILE	     *file;
	Elf32_Ehdr	  ehdr;
	Elf32_Phdr	   *phdrs;
	char	     *base;
	size_t		  size = 0;
	Elf32_Dyn	  *dyn;
	char	     *strtab = NULL;
	const Elf32_Sym	*symtab = NULL;
	size_t		  symtab_size = 0;
	const Elf32_Word *hashtab = NULL;
	const Elf32_Rela *rela;
	size_t		  relacount;

	for (mh = (void *)SysBase->MemList.lh_Head; mh->mh_Node.ln_Succ != NULL;
	     mh = (void *)mh->mh_Node.ln_Succ) {
		printf("usable memory region %p-%p\n", mh->mh_Lower,
		    mh->mh_Upper);
	}

	file = fopen("vmscalux", "rb");
	if (file == NULL)
		err("failed to open vmscalux");

	if (fread(&ehdr, 1, sizeof ehdr, file) != sizeof ehdr)
		err("failed to read executable header");

	assert(memcmp(ehdr.e_ident, ELFMAG, sizeof ELFMAG) == 0);
	assert(ehdr.e_machine == EM_68K);

	phdrs = alloca(sizeof(*phdrs) * ehdr.e_phnum);
	for (int i = 0; i < ehdr.e_phnum; i++) {
		if (fread(&phdrs[i], 1, sizeof phdrs[i], file) !=
		    sizeof phdrs[i])
			err("failed to read a program header");
		printf("program header: type %d, virt 0x%x, size 0x%x\n",
		    phdrs[i].p_type, phdrs[i].p_vaddr, phdrs[i].p_memsz);
		size = MAX(size, phdrs[i].p_vaddr + phdrs[i].p_memsz);
	}

	size += PGSIZE;
	printf("allocating 0x%x bytes for loading\n", size);

	base = AllocMem(size, MEMF_FAST | MEMF_REVERSE);
	if (base == NULL)
		err("failed to allocate memory");

	base = (char *)ROUNDUP(base, PGSIZE);

	for (int i = 0; i < ehdr.e_phnum; i++) {
		Elf32_Phdr *phdr = &phdrs[i];

		if (phdr->p_type == PT_LOAD) {
			size_t excess;

			if (fseek(file, phdr->p_offset, SEEK_SET) == -1 ||
			    fread(base + (phdr->p_vaddr), 1, phdr->p_filesz,
				file) != phdr->p_filesz)
				err("failed to read a load segment");

			excess = phdr->p_memsz - phdr->p_filesz;
			if (excess > 0)
				memset(base + (phdr->p_vaddr) - excess, 0x0,
				    excess);
		} else if (phdr->p_type == PT_DYNAMIC) {
			dyn = (Elf32_Dyn *)(base + phdr->p_vaddr);
		} else if (phdr->p_type == PT_NOTE ||
		    phdr->p_type == PT_INTERP || phdr->p_type == PT_PHDR ||
		    phdr->p_type == PT_GNU_EH_FRAME ||
		    phdr->p_type == PT_GNU_STACK ||
		    phdr->p_type == PT_GNU_RELRO)
			/* epsilon */;
		else {
			printf("warning: unrecognised program header type %d;"
			       "ignoring",
			    phdr->p_type);
		}
	}

	for (int i = 0; dyn[i].d_tag != DT_NULL; i++) {
		switch (dyn[i].d_tag) {
		case DT_STRTAB:
			strtab = base + dyn[i].d_un.d_ptr;
			break;

		case DT_SYMTAB:
			symtab = (const Elf32_Sym *)(base + dyn[i].d_un.d_ptr);
			break;

		case DT_HASH:
			hashtab = (const Elf32_Word *)(base +
			    dyn[i].d_un.d_ptr);
			symtab_size = hashtab[1];
			break;

		case DT_RELA:
			rela = (const Elf32_Rela *)(base + dyn[i].d_un.d_ptr);
			break;

		case DT_RELACOUNT:
			relacount = dyn[i].d_un.d_val;
			break;

		default:
			printf("warning: unknown dyn tag %d\n", dyn[i].d_tag);
		}
	}

	assert(symtab && symtab_size && strtab && rela && relacount);

#if 0
	for (int i = 0; i < symtab_size; i++) {
		const char *symname = strtab + symtab[i].st_name;
	}
#endif
	for (int i = 0; i < relacount; i++) {
		uint32_t *dest = (uint32_t *)(base + rela[i].r_offset);
		assert(ELF32_R_TYPE(rela[i].r_info) == R_68K_RELATIVE);
		*dest = (uint32_t)(base + rela[i].r_addend);
	}

	(void)setup_dttr;

	printf("loaded at %p; calling entry\n", base);

	int (*init)(void) = (int (*)(void))ehdr.e_entry + base;
	printf("R: %d\n", init());

	return 42;
}
