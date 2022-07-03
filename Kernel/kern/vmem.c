#include <sys/queue.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef int	    spl_t;
typedef uintptr_t   vmem_addr_t;
typedef size_t	    vmem_size_t;
typedef struct vmem vmem_t;

typedef enum vmem_flag {
	kVMemSleep = 0x0,
	kVMemNoSleep = 0x1,
} vmem_flag_t;

// clang-format off
typedef vmem_addr_t (*vmem_alloc_t)(vmem_t *vmem, vmem_size_t size,
    vmem_flag_t flags);
typedef void (*vmem_free_t)(vmem_t *vmem, vmem_size_t size, vmem_flag_t flags);
// clang-format on

enum { kNFreeLists = 16 };

typedef struct vmem_seg {

} vmem_seg_t;

TAILQ_HEAD(vmem_seg_queue, vmem_seg);

typedef struct vmem {
	char	    name[64]; /** identifier for debugging */
	vmem_addr_t base;     /** base address */
	vmem_size_t size;     /** size in bytes */
	vmem_size_t quantum;  /** minimum allocation size */

	vmem_flag_t flags;

	vmem_alloc_t allocfn; /** allocate from ::source */
	vmem_free_t  freefn;  /** release to :: source */
	vmem_t      *source;  /** backing source to allocate from */

	struct vmem_seg_queue freequeue[kNFreeLists]; /** power of 2 freelist */
	struct vmem_seg_queue segqueue;		      /** all segments */
} vmem_t;

vmem_t *
vmem_init(vmem_t *vmem, const char *name, vmem_addr_t base, vmem_size_t size,
    vmem_size_t quantum, vmem_alloc_t allocfn, vmem_free_t freefn,
    vmem_t *source, size_t qcache_max, vmem_flag_t flags, spl_t spl)
{
	strcpy(vmem->name, name);
	vmem->base = base;
	vmem->size = size;
	vmem->quantum = quantum;
	vmem->flags = flags;
	vmem->allocfn = allocfn;
	vmem->freefn = freefn;
	vmem->source = source;
}
