/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#ifndef TMPSFS_H_
#define TMPSFS_H_

#include "vfs.h"

typedef struct tmpdirent {
	TAILQ_ENTRY(tmpdirent) entries;

	char	     *name;
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
