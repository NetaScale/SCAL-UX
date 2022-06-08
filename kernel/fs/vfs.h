#ifndef VNODE_H_
#define VNODE_H_

#include <sys/vm.h>

typedef struct vnode vnode_t;

typedef enum vtype { VNON, VREG, VDIR } vtype_t;

struct vnops {
	int (*create)(vnode_t *, vnode_t **, const char *);
	int (*lookup)(vnode_t *, vnode_t **, const char *);
};

typedef struct vnode {
	vtype_t type;
	vm_object_t *vmobj;
	void *data; /* fs-private data */
	struct vnops *ops;
} vnode_t;

typedef struct vfs {
	void *data; /* fs-private data */
} vfs_t;

extern vnode_t *root_vnode;

void tmpfs_mountroot();

#endif /* VNODE_H_ */
