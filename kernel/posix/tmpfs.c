/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <sys/param.h>

#include <dirent.h>
#include <errno.h>
#include <string.h>

#include "kern/liballoc.h"
#include "kern/vm.h"
#include "libkern/klib.h"
#include "posix/dev.h"
#include "posix/tmpfs.h"
#include "posix/vfs.h"
#include "sys/queue.h"
#include "posix/specfs.h"

extern struct vnops tmpfs_vnops;
extern struct vnops tmpfs_spec_vnops;

/*
 * vfsops
 */

int
tmpfs_vget(vfs_t *vfs, vnode_t **vout, ino_t ino)
{
	tmpnode_t *node = (tmpnode_t *)ino;

	if (node->vn != NULL) {
		node->vn->refcnt++;
		*vout = node->vn;
		return 0;
	} else {
		vnode_t *vn = kmalloc(sizeof *vn);
		node->vn = vn;
		vn->refcnt = 1;
		vn->type = node->attr.type;
		vn->ops = vn->type == VCHR ? &tmpfs_spec_vnops : &tmpfs_vnops;
		if (node->attr.type == VREG) {
			vn->vmobj = node->reg.vmobj;
		} else if (node->attr.type == VCHR) {
			spec_setup_vnode(vn, node->chr.dev);
		}
		vn->data = node;
		*vout = vn;
		return 0;
	}
}

void
tmpfs_mountroot()
{
	tmpnode_t *root = kmalloc(sizeof *root);

	root->attr.type = VDIR;
	root->vn = NULL;
	TAILQ_INIT(&root->dir.entries);

	tmpfs_vget(NULL, &root_vnode, (ino_t)root);
}

/*
 * vnops
 */

#define VNTOTN(VN) ((tmpnode_t *)VN->data)

static tmpdirent_t *
tlookup(tmpnode_t *node, const char *filename)
{
	tmpdirent_t *dent;
	TAILQ_FOREACH (dent, &node->dir.entries, entries) {
		if (strcmp(dent->name, filename) == 0)
			return dent;
	}
	return NULL;
}

static tmpnode_t *
tmakenode(tmpnode_t *dn, vtype_t type, const char *name, dev_t dev,
    vattr_t *attr)
{
	tmpnode_t	  *n = kmalloc(sizeof *n);
	tmpdirent_t *td = kmalloc(sizeof *td);

	td->name = strdup(name);
	td->node = n;

	if (attr) {
		n->attr = *attr;
	} else {
		memset(&n->attr, 0x0, sizeof(n->attr));
	}

	n->attr.type = type;
	n->attr.size = 0;
	n->vn = NULL;

	switch (type) {
	case VREG:
		/* vnode object is associated as soon as needed */
		n->reg.vmobj = vm_aobj_new(UINT32_MAX);
		n->reg.vmobj->refcnt++;
		break;

	case VDIR:
		TAILQ_INIT(&n->dir.entries);
		n->dir.parent = dn;
		break;

	case VCHR:
		n->chr.dev = dev;
		break;

	default:
		assert("unreached");
	}

	TAILQ_INSERT_TAIL(&dn->dir.entries, td, entries);

	return n;
}

static int
tmp_create(vnode_t *dvn, vnode_t **out, const char *pathname, vattr_t *attr)
{
	tmpnode_t *n;

	assert(dvn->type == VDIR);

	n = tmakenode(VNTOTN(dvn), VREG, pathname, 0, attr);
	assert(n != NULL);

	return tmpfs_vget(NULL, out, (ino_t)n);
}

static int
tmp_fallocate(vnode_t *vn, off_t off, size_t len)
{
	tmpnode_t *n = VNTOTN(vn);

	if (vn->type != VREG)
		return -ENOTSUP;

	if (off + len > n->attr.size) 
		n->attr.size = off + len;

	return 0;
}

int
tmp_lookup(vnode_t *vn, vnode_t **out, const char *pathname)
{
	tmpnode_t	  *node = VNTOTN(vn);
	tmpdirent_t *tdent;
	int	     r;

	assert(node->attr.type == VDIR);

	if (strcmp(pathname, "..") == 0) {
		if (!node->dir.parent)
			*out = vn;
			else
		*out = node->dir.parent->vn;
		return 0;
	}

	tdent = tlookup(node, pathname);
	if (!tdent)
		return -ENOENT;

	r = tmpfs_vget(NULL, out, (ino_t)tdent->node);

	return r;
}

int
tmp_getattr(vnode_t *vn, vattr_t *out)
{
	tmpnode_t *tn = VNTOTN(vn);
	*out = tn->attr;
	return 0;
}

int
tmp_mkdir(vnode_t *dvn, vnode_t **out, const char *pathname, struct vattr *attr)
{
	tmpnode_t *n;

	assert(dvn->type == VDIR);

#ifdef DEBUG_TMPFS_VNOPS
	kprintf("tmp_mkdir vnode %p path %s\n", dvn, pathname);
#endif

	n = tmakenode(VNTOTN(dvn), VDIR, pathname, 0, attr);
	assert(n != NULL);

	return tmpfs_vget(NULL, out, (ino_t)n);
}

int
tmp_mknod(vnode_t *dvn, vnode_t **out, const char *pathname, dev_t dev)
{
	tmpnode_t *n;

	assert(dvn->type == VDIR);

	n = tmakenode(VNTOTN(dvn), VCHR, pathname, dev, NULL);
	assert(n != NULL);

	return tmpfs_vget(NULL, out, (ino_t)n);
}

int
tmp_read(vnode_t *vn, void *buf, size_t nbyte, off_t off)
{
	vaddr_t vaddr = VADDR_MAX;
	tmpnode_t *tn = VNTOTN(vn);

	if (tn->attr.type != VREG)
		return -EINVAL;

	if (off + nbyte > tn->attr.size)
		nbyte = tn->attr.size <= off ? 0 : tn->attr.size - off;
	if (nbyte == 0)
		return 0;

	assert(vm_map_object(&kmap, vn->vmobj, &vaddr, PGROUNDUP(nbyte + off),
		   0, false) == 0);
	memcpy(buf, vaddr + off, nbyte);
	vm_deallocate(&kmap, vaddr, PGROUNDUP(nbyte + off));

	return nbyte; /* FIXME */

#if 0
	/* todo move to vfs_cached_read, this is generic pagecache manipulation? */
	voff_t base = PGROUNDDOWN(off);
	voff_t pageoff = off - base;
	size_t firstpage = base / PGSIZE;
	size_t lastpage = firstpage + (pageoff + nbyte - 1) / PGSIZE + 1;

	for (size_t page = firstpage; page < lastpage; page++) {
		vm_anon_t *anon;
		int	   r;
		size_t	   tocopy;

		if (nbyte > PGSIZE)
			tocopy = PGSIZE - pageoff;
		else
			tocopy = nbyte;

		r = tmp_getpage(vn, page * PGSIZE, &anon, false);
		if (r < 0)
			return r;

		memcpy(buf + (page - firstpage) * PGSIZE,
		    P2V(anon->physpg->paddr) + pageoff, tocopy);

		nbyte -= tocopy;
		pageoff = 0;
	}

	return nbyte;

#endif
}

int
tmp_write(vnode_t *vn, void *buf, size_t nbyte, off_t off)
{
	vaddr_t vaddr = VADDR_MAX;
	tmpnode_t *tn = VNTOTN(vn);

	if (nbyte == 0)
		return 0;

	if (off + nbyte > tn->attr.size)
		tn->attr.size = off + nbyte;

	assert(vm_map_object(&kmap, vn->vmobj, &vaddr, PGROUNDUP(nbyte + off),
		   0, false) == 0);
	memcpy(vaddr + off, buf, nbyte);
	vm_deallocate(&kmap, vaddr, PGROUNDUP(nbyte + off));

	return nbyte;
#if 0
	/* todo move to vfs_cached_write, this is generic pagecache manipulation? */
	/* FIXME: fix offset writes after the example of tmp_read */
	voff_t base = PGROUNDDOWN(off);
	voff_t pageoff = off - base;
	size_t npages = (pageoff + nbyte - 1 /* ?? */) / PGSIZE + 1;

	if (off + nbyte > vn->attr.size)
		vn->attr.size = off + nbyte;


	for (size_t page = base / PGSIZE; page < npages; page++) {
		vm_anon_t *anon;
		int	   r;
		size_t	   tocopy;

		if (nbyte > PGSIZE)
			tocopy = PGSIZE - pageoff;
		else
			tocopy = nbyte;

		r = tmp_getpage(vn, page * PGSIZE, &anon, false);
		if (r < 0)
			return r;

		memcpy(P2V(anon->physpg->paddr) + pageoff, buf + page * PGSIZE,
		    tocopy);

		nbyte -= tocopy;
		pageoff = 0;
	}
#endif

	return nbyte /* FIXME: */;
}

#define DIRENT_RECLEN(NAMELEN) \
	ROUNDUP(offsetof(struct dirent, d_name[0]) + 1 + NAMELEN, 8)

int
tmp_readdir(vnode_t *dvn, void *buf, size_t nbyte, size_t *bytesRead,
    off_t seqno)
{
	tmpnode_t	  *n = VNTOTN(dvn);
	tmpdirent_t	    *tdent;
	struct dirent     *dentp = buf;
	size_t		   nwritten = 0;
	size_t		   i;

	assert(n->attr.type == VDIR);

	tdent = TAILQ_FIRST(&n->dir.entries);

	for (i = 0;; i++) {
		if (!tdent) {
			i = INT32_MAX;
			goto finish;
		}

		if (i >= seqno) {
			size_t reclen = DIRENT_RECLEN(strlen(tdent->name));

			if ((void *)dentp + reclen > buf + nbyte - 1) {
				i--;
				goto finish;
			}

			dentp->d_ino = (uint64_t)tdent->node;
			dentp->d_off = i++;
			dentp->d_reclen = reclen;
			dentp->d_type = DT_UNKNOWN;
			strcpy(dentp->d_name, tdent->name);

			nwritten += reclen;
			dentp = (void *)dentp + reclen;
		}

		tdent = TAILQ_NEXT(tdent, entries);
	}

finish:
	*bytesRead = nwritten;
	return i;
}

struct vnops tmpfs_vnops = {
	.create = tmp_create,
	.fallocate = tmp_fallocate,
	.lookup = tmp_lookup,
	.getattr = tmp_getattr,
	.mkdir = tmp_mkdir,
	.mknod = tmp_mknod,
	.read = tmp_read,
	.write = tmp_write,
	.readdir = tmp_readdir,
};

struct vnops tmpfs_spec_vnops = {
	.getattr = tmp_getattr,
	.open = spec_open,
	.read = spec_read,
	.write = spec_write,
	.kqfilter = spec_kqfilter,
};
