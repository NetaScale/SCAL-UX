/*
 * support for loadable kernel servers
 */

#include <elf.h>

#include "kern/ksrv.h"
#include "kern/vm.h"
#include "libkern/klib.h"
#include "sys/vm.h"

#define ELFMAG "\177ELF"
#define MAX2(x, y) ((x > y) ? x : y)

struct kmod_head kmods;

static uint32_t
elf64_hash(const char *name)
{
	uint32_t h = 0, g;
	for (; *name; name++) {
		h = (h << 4) + (uint8_t)*name;
		g = h & 0xf0000000;
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

/*
 * Lookup a symbol, searching each kmod to check whether it contains it.
 *
 * assumes: no duplicate names
 */
static const void *
kmod_lookupsym(const char *name)
{
	kmod_t *kmod;

	TAILQ_FOREACH (kmod, &kmods, entries) {
		const Elf64_Sym *sym = NULL;
		int		 bind;

		if (kmod->hashtab)
			sym = elf64_hashlookup(kmod->symtab, kmod->strtab,
			    kmod->hashtab, name);
		else if (kmod->symtab)
			for (int i = 0; i < kmod->symtab_size; i++) {
				const char *cand = kmod->strtab +
				    kmod->symtab[i].st_name;
				if (strcmp(name, cand) == 0) {
					sym = &kmod->symtab[i];
					break;
				}
			}

		if (!sym)
			continue;

		bind = ELF64_ST_BIND(sym->st_info);
		if (bind != STB_GLOBAL && bind != STB_WEAK &&
		    bind != STB_GNU_UNIQUE) {
			kprintf("binding for %s is not global/weak/unique",
			    name);
			continue;
		}

		if (kmod->hashtab) /* meaning is a shared library */
			return kmod->base + sym->st_value;
		else
			return (void *)
			    sym->st_value; /* kernel addrs are absolute */
	}

	return NULL;
}

int
ksrv_backtrace(vaddr_t vaddr, const char **name, size_t *offset)
{
	const char *cand_name;
	vaddr_t	    cand_addr = NULL;
	size_t	    cand_offs;
	kmod_t     *kmod;

	TAILQ_FOREACH (kmod, &kmods, entries) {
		for (int i = 0; i < kmod->symtab_size; i++) {
			const char *iName = kmod->strtab +
			    kmod->symtab[i].st_name;
			vaddr_t iAddr = (vaddr_t)kmod->symtab[i].st_value;

			if (iAddr <= vaddr && iAddr > cand_addr) {
				cand_name = iName;
				cand_addr = iAddr;
				cand_offs = vaddr - iAddr;
			}
		}
	}

	if (cand_addr == NULL)
		return -1;

	*name = cand_name;
	*offset = cand_offs;
	return 0;
}

void
ksrv_parsekern(void *addr)
{
	Elf64_Ehdr *ehdr = addr;
	kmod_t     *kmod = kmalloc(sizeof *kmod);

	kprintf("reading kernel: addr %p...\n", addr);

	TAILQ_INIT(&kmods);
	TAILQ_INSERT_HEAD(&kmods, kmod, entries);

	for (int x = 0; x < ehdr->e_shentsize * ehdr->e_shnum;
	     x += ehdr->e_shentsize) {
		Elf64_Shdr *shdr = (addr + ehdr->e_shoff + x);
		if (shdr->sh_type == SHT_SYMTAB) {
			Elf64_Shdr *strtabshdr;

			assert(shdr->sh_entsize == sizeof(Elf64_Sym));

			kmod->symtab = addr + shdr->sh_offset;
			kmod->symtab_size = shdr->sh_size / sizeof(Elf64_Sym);

			strtabshdr = addr + ehdr->e_shoff +
			    ehdr->e_shentsize * shdr->sh_link;
			kmod->strtab = addr + strtabshdr->sh_offset;

			break;
		}
	}

	assert(kmod->symtab != NULL);
	kmod->hashtab = NULL;
}

/* return true if this reloc type doesn't need a symbol resolved */
bool
reloc_need_resolution(int type)
{
	switch (type) {
	case R_X86_64_RELATIVE:
		return false;

	default:
		return true;
	}
}

static int
do_reloc(kmod_t *kmod, const Elf64_Rela *reloc)
{
	uint64_t	 *dest = (uint64_t *)(kmod->base + reloc->r_offset);
	unsigned int	 symn = ELF64_R_SYM(reloc->r_info);
	const Elf64_Sym *sym = &kmod->symtab[symn];
	const char	   *symname = kmod->strtab + sym->st_name;
	const void	   *symv = 0x0; /* symbol virtual address */
	int		 type = ELF64_R_TYPE(reloc->r_info);

	kprintf("sym %s: relocation type %lu/symidx %hu: ", symname,
	    ELF64_R_TYPE(reloc->r_info), sym->st_shndx);

	if (reloc_need_resolution(type) && sym->st_shndx == SHN_UNDEF) {
		kprintf("undefined, resolving globally:\n");
		symv = kmod_lookupsym(symname);
		if (!symv) {
			kprintf("missing symbol %s, quitting\n", symname);
			while (true)
				asm("pause");
			return -1;
		} else {
			kprintf("resolved to %p\n", symv);
		}
	} else {
		symv = kmod->base + sym->st_value;
		kprintf("0x%lx -> %p\n", sym->st_value, symv);
	}

	switch (type) {
	case R_X86_64_64:
		*dest = (uint64_t)symv + reloc->r_addend;
		break;

	case R_X86_64_GLOB_DAT:
	case R_X86_64_JUMP_SLOT:
		*dest = (uint64_t)symv;
		break;

	case R_X86_64_RELATIVE:
		*dest = (uint64_t)kmod->base + reloc->r_addend;
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
	kmod_t	   kmod = { 0 };
	void (*mod_init)(void) = NULL;
	void (**initfns)(void) = NULL;
	size_t initfnscnt;

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

	kmod.base = VADDR_MAX;
	vm_object_t *obj;
	vm_allocate(&kmap, &obj, &kmod.base, kmod.mem_size);

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

		case DT_INIT_ARRAY:
			initfns = (void (**)(void))(
			    kmod.base + ent->d_un.d_ptr);
			break;

		case DT_INIT_ARRAYSZ:
			initfnscnt = ent->d_un.d_val / sizeof(void (*)(void));
			break;

		/* ignore the rest */
		default:
			break;
		}
	}

	for (int i = 0; i < kmod.symtab_size; i++) {
		const char *symname = kmod.strtab + kmod.symtab[i].st_name;
		if (strcmp(symname, "modinit") == 0)
			mod_init = (void *)(kmod.symtab[i].st_value +
			    kmod.base);
	}

	kprintf("looking at reloc tables\n");
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
		}
	}

	for (int i = 0; i < initfnscnt; i++) {
		kprintf("calling initfn %d\n", i);
		initfns[i]();
	}
	kprintf("calling modinit (%p)\n\n", mod_init);
	mod_init();
	kprintf("modinit done\n");
}
