/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

/*!
 * SpecFS provides for the ability to have multiple vnodes referencing a single
 * underlying device.
 */

#ifndef SPECFS_H_
#define SPECFS_H_

#include <sys/queue.h>

#include "dev.h"

typedef struct specdev {
	LIST_ENTRY(specdev) queue;
	dev_t dev;
	LIST_HEAD(, vnode) vnodes;
} specdev_t;

/*! Setup a (per-actual-file-referencing-the-device) vnode. */
void spec_setup_vnode(struct vnode *vn, dev_t dev);

int spec_open(struct vnode *vn, int mode, struct proc *proc);
int spec_read(struct vnode *vn, void *buf, size_t nbyte, off_t off);
int spec_write(struct vnode *vn, void *buf, size_t nbyte, off_t off);
int spec_kqfilter(struct vnode *vn, struct knote *kn);

#endif /* SPECFS_H_ */
