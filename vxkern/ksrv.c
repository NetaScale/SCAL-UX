/*
 * support for loadable kernel servers
 */

#include "klibc.h"
#include "ksrv.h"

#define MAX2(x, y) ((x > y) ? x : y)

kmod_t *kmods;

static void
doRela()
{
	kprintf("hello from doRela\n");
}

static uint32_t
elf64_hash(const char *name)
{
	uint32_t h = 0, g;
	for (; *name; name++) {
		h = (h << 4) + (uint8_t)*name;
		uint32_t g = h & 0xf0000000;
		if (g)
			h ^= g >> 24;
		h &= 0x0FFFFFFF;
	}
	return h;
}

static const Elf64_Sym *
elf64_hashlookup(const Elf64_Sym *symtab, const char *strtab,
    const uint32_t *hashtab, const char *symname)
{
	const uint32_t hash = elf64_hash(symname);
	const uint32_t nbucket = hashtab[0];

	for (uint32_t i = hashtab[2 + hash % nbucket]; i;
	     i = hashtab[2 + nbucket + i]) {
		if (strcmp(symname, strtab + symtab[i].st_name) == 0)
			return &symtab[i];
	}

	return NULL;
}

void
kmod_parsekern(void *addr)
{
	Elf64_Ehdr *ehdr = addr;
	kmod_t kmod = { 0 };

	kprintf("reading kernel: addr %p...\n", addr);

	for (int x = 0; x < ehdr->e_shentsize * ehdr->e_shnum;
	     x += ehdr->e_shentsize) {
		Elf64_Shdr *shdr = (addr + ehdr->e_shoff + x);
		if (shdr->sh_type == SHT_SYMTAB) {
			Elf64_Shdr *strtabshdr;

			assert(shdr->sh_entsize == sizeof(Elf64_Sym));

			kmod.symtab = addr + shdr->sh_offset;
			kmod.symtab_size = shdr->sh_size / sizeof(Elf64_Sym);

			strtabshdr = addr + ehdr->e_shoff +
			    ehdr->e_shentsize * shdr->sh_link;
			kmod.strtab = addr + strtabshdr->sh_offset;

			break;
		}
	}

	assert(kmod.symtab != NULL);
}

static int
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
kmod_load(void *addr)
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
			/* epsilon */;
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