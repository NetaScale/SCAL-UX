
#include <sys/param.h>

#include <errno.h>
#include <string.h>

#include "kern/kern.h"
#include "kern/liballoc.h"
#include "kern/vm.h"
#include "tmpfs.h"

extern struct vnops tmpfs_vnops;

/*
 * vfsops
 */

int
tmpfs_vget(vfs_t *vfs, vnode_t **vout, ino_t ino)
{
	tmpnode_t *node = (tmpnode_t *)ino;

	if (node->vn != NULL) {
		*vout = node->vn;
		return 0;
	} else {
		vnode_t *vn = kmalloc(sizeof *vn);
		vn->refcnt = 1;
		vn->type = node->type;
		vn->ops = &tmpfs_vnops;
		vn->attr.size = node->size;
		if (node->type == VREG) {
			vn->vmobj = node->reg.vmobj;
			node->reg.vmobj->anon.vnode = vn;
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

	root->type = VDIR;
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
tmakenode(tmpnode_t *dn, vtype_t type, const char *name)
{
	tmpnode_t *n = kmalloc(sizeof *n);
	tmpdirent_t *td = kmalloc(sizeof *td);

	td->name = strdup(name);
	td->node = n;
	n->type = type;
	n->size = 0;

	switch (type) {
	case VREG:
		/* vnode object is associated as soon as needed */
		vm_object_new_anon(&n->reg.vmobj, INT32_MAX, &vm_vnode_pagerops,
		    NULL);
		break;

	case VDIR:
		TAILQ_INIT(&n->dir.entries);
		n->dir.parent = dn;
		break;

	default:
		assert("unreached");
	}

	TAILQ_INSERT_TAIL(&dn->dir.entries, td, entries);

	return n;
}

static int
tmp_create(vnode_t *dvn, vnode_t **out, const char *pathname)
{
	tmpnode_t *n;

	assert(dvn->type == VDIR);

	n = tmakenode(VNTOTN(dvn), VREG, pathname);
	assert(n != NULL);

	return tmpfs_vget(NULL, out, (ino_t)n);
}

static int
tmp_fallocate(vnode_t *vn, off_t off, size_t len)
{
	tmpnode_t *n = VNTOTN(vn);

	if (vn->type != VREG)
		return -ENOTSUP;

	if (off + len > n->size) {
		vn->attr.size = off + len;
		n->size = off + len;
	}

	return 0;
}

int
tmp_lookup(vnode_t *vn, vnode_t **out, const char *pathname)
{
	tmpnode_t *node = VNTOTN(vn);
	tmpdirent_t *tdent;
	int r;

	assert(node->type == VDIR);

	if (strcmp(pathname, "..") == 0) {
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
tmp_getpage(vnode_t *vn, voff_t a_off, vm_anon_t **out, bool needcopy)
{
	return vm_anon_pagerops.get(vn->vmobj, a_off, out, needcopy);
}

int
tmp_read(vnode_t *vn, void *buf, size_t nbyte, off_t off)
{
	/* todo move to vfs_read, this is generic pagecache manipulation */
	voff_t base = PGROUNDDOWN(off);
	voff_t pageoff = off - base;
	size_t npages = (pageoff + nbyte) / PGSIZE;

	assert(off + nbyte <= vn->attr.size);

	for (size_t page = base / PGSIZE; page < npages; page++) {
		vm_anon_t *anon;
		int r;
		size_t tocopy;

		if (nbyte > PGSIZE)
			tocopy = PGSIZE - pageoff;
		else
			tocopy = nbyte;

		r = tmp_getpage(vn, page * PGSIZE, &anon, false);
		if (r < 0)
			return r;

		memcpy(buf + page * PGSIZE, P2V(anon->physpg->paddr), tocopy);

		nbyte -= tocopy;
		pageoff = 0;
	}

	return 0;
}

int
tmp_write(vnode_t *vn, void *buf, size_t nbyte, off_t off)
{
	/* todo move to vfs_write, this is generic pagecache manipulation */
	voff_t base = PGROUNDDOWN(off);
	voff_t pageoff = off - base;
	size_t npages = (pageoff + nbyte) / PGSIZE;

	assert(off + nbyte <= vn->attr.size);

	for (size_t page = base / PGSIZE; page < npages; page++) {
		vm_anon_t *anon;
		int r;
		size_t tocopy;

		if (nbyte > PGSIZE)
			tocopy = PGSIZE - pageoff;
		else
			tocopy = nbyte;

		r = tmp_getpage(vn, page * PGSIZE, &anon, false);
		if (r < 0)
			return r;

		memcpy(P2V(anon->physpg->paddr), buf + page * PGSIZE, tocopy);

		nbyte -= tocopy;
		pageoff = 0;
	}

	return 0;
}

struct vnops tmpfs_vnops = {
	.create = tmp_create,
	.fallocate = tmp_fallocate,
	.lookup = tmp_lookup,
	.getpage = tmp_getpage,
	.read = tmp_read,
	.write = tmp_write,
};
