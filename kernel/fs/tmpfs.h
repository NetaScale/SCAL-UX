#ifndef TMPSFS_H_
#define TMPSFS_H_

#include "vfs.h"

typedef uintptr_t ino_t;

typedef struct tmpdirent {
	TAILQ_ENTRY(tmpdirent) entries;

	char *name;
	struct tmpnode *node;
} tmpdirent_t;

typedef struct tmpnode {
	vtype_t type;
	/* associated vnode; may be null. shares \l obj with this */
	vnode_t *vn;

	union {
		/* VDIR case */
		struct {
			TAILQ_HEAD(, tmpdirent) entries;
			struct tmpnode *parent;
		} dir;

		/* VREG case */
		struct {
			vm_object_t *vmobj;
		} reg;
	};
} tmpnode_t;

#endif /* TMPSFS_H_ */
