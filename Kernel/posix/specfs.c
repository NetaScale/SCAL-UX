#include "kern/liballoc.h"
#include "kern/lock.h"
#include "machine/spl.h"
#include "posix/specfs.h"
#include "posix/vfs.h"

/* FIXME should be a mutex since we kmalloc here */
spinlock_t spec_lock = SPINLOCK_INITIALISER;
LIST_HEAD(, specdev) specdevs = LIST_HEAD_INITIALIZER(&specdevs);

void
spec_setup_vnode(struct vnode *vn, dev_t dev)
{
	spl_t	   spl = splhigh();
	specdev_t *specdev;

	lock(&spec_lock);
	LIST_FOREACH (specdev, &specdevs, queue) {
		if (specdev->dev == dev)
			goto found;
	}

	/* not found, create */
	specdev = kmalloc(sizeof *specdev);
	specdev->dev = dev;
	LIST_INIT(&specdev->vnodes);
	LIST_INSERT_HEAD(&specdevs, specdev, queue);

found:
	vn->specdev = specdev;
	LIST_INSERT_HEAD(&specdev->vnodes, vn, spec_list);

	unlock(&spec_lock);
	splx(spl);
}

int
spec_open(vnode_t *vn, int mode, struct proc *proc)
{
	return cdevsw[major(vn->specdev->dev)].open(vn->specdev->dev, mode,
	    proc);
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
