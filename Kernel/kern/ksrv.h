#ifndef KSRV_H_
#define KSRV_H_

#include <sys/queue.h>

#include <elf.h>
#include <stddef.h>

#include "kern/vm.h"

typedef struct kmod {
	TAILQ_ENTRY(kmod) entries;

	vaddr_t base;
	size_t	mem_size; /* total size of virt address space */

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
	size_t		 symtab_size;
} kmod_t;

extern TAILQ_HEAD(kmod_head, kmod) kmods;

/** Parse the kernel executable, adding it to the kmod table. */
void ksrv_parsekern(void *addr);

/**
 * Find the greatest-addressed symbol lower than the given address, and the
 * offset from this. Returns -1 if no symbol is lower than the address.
 *
 * @param[out] name Name of the symbol.
 * @param[out] offset Offset from symbol to address.
 */
int ksrv_backtrace(vaddr_t vaddr, const char **name, size_t *offset);

#endif /* KSRV_H_ */
