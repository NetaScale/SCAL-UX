#ifndef VNODE_H_
#define VNODE_H_

#include <sys/types.h>

#include "kern/vm.h"

typedef struct vnode vnode_t;

typedef enum vtype { VNON, VREG, VDIR } vtype_t;

struct vnops {
	/**
	 * Create a new VREG file vnode in the given directory.
	 *
	 * @param dvn LOCKED directory vnode
	 * @param out [out] resultant vnode (add a ref for caller)
	 * @param name new file name
	 */
	int (*create)(vnode_t *dvn, vnode_t **out, const char *name);
	/**
	 * Lookup the vnode corresponding to the given file name in the given
	 * direct vnode.
	 *
	 * @param dvn LOCKED directory vnode
	 * @param out [out] resultant vnode (add a ref for caller)
	 * @param 3 filename
	 */
	int (*lookup)(vnode_t *dvn, vnode_t **out, const char *name);
	/**
	 * Get a page from a VREG vnode's backing store or cache.
	 *
	 * @param dvn LOCKED directory vnode
	 * @param off offset in bytes into the vnode (will be multiple of
	 * PGSIZE)
	 * @param out [out] resultant anonymous page
	 * @param needcopy whether this is for a copy-on-write mapping (i.e.
	 * the anon should not be from or entered into the cache as it'll be
	 * written to forthwith).
	 */
	int (*getpage)(vnode_t *dvn, voff_t off, vm_anon_t**out, bool needcopy);
};

typedef struct vnode {
	size_t refcnt;
	vtype_t type;
	vm_object_t *vmobj;
	void *data; /* fs-private data */
	struct vnops *ops;
	spinlock_t interlock;
} vnode_t;

/**
 * Represents a mounted filesystem.
 */
typedef struct vfs {
	void *data; /* fs-private data */
} vfs_t;

extern vnode_t *root_vnode;

void tmpfs_mountroot();

#endif /* VNODE_H_ */
