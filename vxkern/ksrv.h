#ifndef KSRV_H_
#define KSRV_H_

#include <sys/elf64.h>
#include <sys/queue.h>

#include <stddef.h>

typedef struct kmod {
	LIST_ENTRY(entry);

	char *base;
	size_t mem_size; /* total size of virt address space */

	Elf64_Dyn *dyn;

	/*
	 * Elf64_Word nbuckets;
	 * Elf64_Word nchain;
	 * Elf64_Word bucket[nbucket];
	 * Elf64_Word chain[nchain];
	 */
	const Elf64_Word *hashtab;

	void (**init_array)(void);
	size_t init_array_size;

	char *strtab;

	const Elf64_Sym *symtab;
	size_t symtab_size;
} kmod_t;

extern kmod_t *kmods;

#endif /* KSRV_H_ */
