#ifndef TMPSFS_H_
#define TMPSFS_H_

#include "vfs.h"

typedef struct tmpdirent {
	TAILQ_ENTRY(tmpdirent) entries;

	char *name;
	struct tmpnode *node;
} tmpdirent_t;

typedef struct tmpnode {
	/** Type of node. */
	vtype_t type;

	/** Associated vnode; may be null. It shares its vmobj with this. */
	vnode_t *vn;

	/* size */
	size_t size;

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

		/* VCHR case */
		struct {
			dev_t dev;
		} chr;
	};
} tmpnode_t;

#endif /* TMPSFS_H_ */
