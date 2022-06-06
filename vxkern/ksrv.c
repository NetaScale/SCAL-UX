/*
 * support for loadable kernel servers
 */

#include <sys/elf64.h>

#include "klibc.h"

#define MAX2(x, y) ((x > y) ? x : y)

typedef struct kmod {
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

static void
doRela()
{
	kprintf("hello from doRela\n");
}

int
do_reloc(kmod_t *kmod, const Elf64_Rela *reloc)
{
	uint64_t *dest = (uint64_t *)(kmod->base + reloc->r_offset);
	unsigned int symn = ELF64_R_SYM(reloc->r_info);
	const Elf64_Sym *sym = &kmod->symtab[symn];
	const char *symname = kmod->strtab + sym->st_name;
	void *symv = 0x0; /* symbol virtual address */

	kprintf("sym %s: relocation: %lu/symidx %hu: ", symname,
	    ELF64_R_TYPE(reloc->r_info), sym->st_shndx);

	if (sym->st_shndx == SHN_UNDEF) {
		kprintf("undefined, resolving globally\n");
		symv = doRela;
	} else {
		symv = kmod->base + sym->st_value;
		kprintf("relocating from 0x%lx to %p\n", sym->st_value, symv);
	}

	switch (ELF64_R_TYPE(reloc->r_info)) {
	case R_X86_64_JMP_SLOT:
		*dest = (uint64_t)symv;
		break;

	default:
		kprintf("Unsupported reloc %ld\n", ELF64_R_TYPE(reloc->r_info));
		return -1;
	}

	return 0;
}

/*
 * current faults:
 * - assumes load kmod.base of 0x0
 */
void
loadelf(void *addr)
{
	Elf64_Ehdr ehdr;
	kmod_t kmod = { 0 };
	void (*mod_init)(void) = NULL;

	kprintf("loading an elf...\n");
	memcpy(&ehdr, addr, sizeof(Elf64_Ehdr));

	if (memcmp(ehdr.e_ident, ELFMAG, 4) != 0) {
		kprintf("bad elf header\n");
		return;
	} else if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
		kprintf("bad class\n");
		return;
	} else if (ehdr.e_type != ET_DYN) {
		kprintf("not a dso: type %hu\n", ehdr.e_type);
		return;
	}

	/* let us first conclude the total size */
	for (int i = 0; i < ehdr.e_phnum; i++) {
		Elf64_Phdr *phdr = addr + ehdr.e_phoff + ehdr.e_phentsize * i;
		kmod.mem_size = MAX2(phdr->p_vaddr + phdr->p_memsz,
		    kmod.mem_size);
	}

	kmod.base = kmalloc(kmod.mem_size);

	for (int i = 0; i < ehdr.e_phnum; i++) {
		Elf64_Phdr phdr;

		memcpy(&phdr, addr + ehdr.e_phoff + ehdr.e_phentsize * i,
		    sizeof(Elf64_Phdr));

		kprintf("phdr: type %u memsz %lu 0x%lx\n", phdr.p_type,
		    phdr.p_memsz, phdr.p_vaddr);

		if (phdr.p_type == PT_LOAD) {
			memset(kmod.base + phdr.p_vaddr, 0, phdr.p_memsz);
			memcpy(kmod.base + phdr.p_vaddr, addr + phdr.p_offset,
			    phdr.p_filesz);
		} else if (phdr.p_type == PT_DYNAMIC)
			kmod.dyn = (Elf64_Dyn *)(kmod.base + phdr.p_vaddr);
		else if (phdr.p_type == PT_NOTE ||
		    phdr.p_type == PT_GNU_EH_FRAME ||
		    phdr.p_type == PT_GNU_STACK || phdr.p_type == PT_GNU_RELRO)
			;
		else
			kprintf("...unrecognised type, ignoring\n");
	}

	for (size_t i = 0; kmod.dyn[i].d_tag != DT_NULL; i++) {
		Elf64_Dyn *ent = &kmod.dyn[i];
		switch (ent->d_tag) {
		case DT_STRTAB:
			kmod.strtab = kmod.base + ent->d_un.d_ptr;
			break;

		case DT_SYMTAB:
			kmod.symtab = (const Elf64_Sym *)(kmod.base +
			    ent->d_un.d_ptr);
			break;

		case DT_HASH:
			kmod.hashtab = (const Elf64_Word *)(kmod.base +
			    ent->d_un.d_ptr);
			kmod.symtab_size = kmod.hashtab[1];
			break;

		/* ignore the rest */
		default:
			break;
		}
	}

	for (int i = 0; i < kmod.symtab_size; i++) {
		const char *symname = kmod.strtab + kmod.symtab[i].st_name;
		kprintf("symbol %s: %p\n", symname, kmod.symtab[i].st_value);
		if (strcmp(symname, "modinit") == 0)
			mod_init = (void *)(kmod.symtab[i].st_value +
			    kmod.base);
	}

	kprintf("begin relocation...\n");
	for (int x = 0; x < ehdr.e_shentsize * ehdr.e_shnum;
	     x += ehdr.e_shentsize) {
		Elf64_Shdr shdr;
		memcpy(&shdr, addr + ehdr.e_shoff + x, ehdr.e_shentsize);

		if (shdr.sh_type == SHT_RELA) {
			const Elf64_Rela *reloc = (Elf64_Rela *)(kmod.base +
			    shdr.sh_addr);
			const Elf64_Rela *lim = (Elf64_Rela *)(kmod.base +
			    shdr.sh_addr + shdr.sh_size);

			for (; reloc < lim; reloc++)
				if (do_reloc(&kmod, reloc) == -1)
					return;
		} else {
			printf("shdr type %d\n", shdr.sh_type);
		}
	}

	kprintf("...loaded..\n\n");
	mod_init();
	kprintf("modinit done\n");
}