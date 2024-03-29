/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <kern/kmem.h>
#include <kern/sync.h>
#include <posix/vfs.h>

#include "dev.h"

cdevsw_t cdevsw[64] = { 0 };
mutex_t	 spec_lock = MUTEX_INITIALISER(spec_lock);
LIST_HEAD(, specdev) specdevs = LIST_HEAD_INITIALIZER(&specdevs);

int
cdevsw_attach(cdevsw_t *bindings)
{
	for (int i = 0; i < 64; i++)
		if (!cdevsw[i].valid) {
			cdevsw[i] = *bindings;
			cdevsw[i].valid = true;
			return i;
		}

	/* out of majors */
	return -1;
}

int
devfs_make_node(dev_t dev, const char *name)
{
	vnode_t *vn;
	vattr_t	 attr;

	attr.mode = 0644 | S_IFCHR;
	attr.type = VCHR;
	attr.rdev = dev;

	return dev_vnode->ops->create(dev_vnode, &vn, name, &attr);
}

void
spec_setup_vnode(struct vnode *vn, dev_t dev)
{
	specdev_t *specdev;

	mutex_lock(&spec_lock);
	LIST_FOREACH (specdev, &specdevs, queue) {
		if (specdev->dev == dev)
			goto found;
	}

	/* not found, create */
	specdev = kmem_alloc(sizeof *specdev);
	specdev->dev = dev;
	LIST_INIT(&specdev->vnodes);
	LIST_INSERT_HEAD(&specdevs, specdev, queue);

found:
	vn->specdev = specdev;
	LIST_INSERT_HEAD(&specdev->vnodes, vn, spec_list);

	mutex_unlock(&spec_lock);
}

int
spec_open(vnode_t *vn, vnode_t **out, int mode)
{
	return cdevsw[major(vn->specdev->dev)].open(vn->specdev->dev, out,
	    mode);
}

int
spec_read(vnode_t *vn, void *buf, size_t nbyte, off_t off)
{
	return cdevsw[major(vn->specdev->dev)].read(vn->specdev->dev, buf,
	    nbyte, off);
}

int
spec_write(vnode_t *vn, void *buf, size_t nbyte, off_t off)
{
	return cdevsw[major(vn->specdev->dev)].write(vn->specdev->dev, buf,
	    nbyte, off);
}

int
spec_kqfilter(vnode_t *vn, struct knote *kn)
{
	return cdevsw[major(vn->specdev->dev)].kqfilter(vn->specdev->dev, kn);
}
