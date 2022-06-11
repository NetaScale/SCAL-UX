
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
	TAILQ_FOREACH(dent, &node->dir.entries, entries)
	{
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
	return 0;
}

struct vnops tmpfs_vnops = {
	.create = tmp_create,
	.lookup = tmp_lookup,
	.getpage = tmp_getpage,
};
