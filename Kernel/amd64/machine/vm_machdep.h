#ifndef VM_MACHDEP_H_
#define VM_MACHDEP_H_

#define PGSIZE 4096

#define HHDM_BASE 0xffff800000000000
#define KHEAP_BASE 0xffff800100000000
#define KERN_BASE 0xffffffff80000000

#define HHDM_SIZE 0x100000000  /* 4GiB */
#define KHEAP_SIZE 0x100000000 /* 4GiB */
#define KERN_SIZE 0x10000000   /* 256MiB */

#define P2V(addr) (((void *)addr) + HHDM_BASE)
#define V2P(addr) (((void *)addr) - HHDM_BASE)

#endif /* VM_MACHDEP_H_ */
