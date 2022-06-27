#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include "dev.h"
#include "kern/liballoc.h"
#include "posix_proc.h"
#include "vfs.h"

vnode_t *root_vnode = NULL;
vnode_t *root_dev = NULL;

#define VOP_READ(vnode, buf, nbyte, off) \
	vnode->ops->read(vnode, buf, nbyte, off)
#define VOP_WRITE(vnode, buf, nbyte, off) \
	vnode->ops->write(vnode, buf, nbyte, off)
#define VOP_LOOKUP(vnode, out, path) vnode->ops->lookup(vnode, out, path)

#define countof(ARRAY) (sizeof(ARRAY) / sizeof(ARRAY[0]))

static void
file_unref(file_t *file)
{
	if (--file->refcnt == 0) {
		// vn_unref(file->vn);
		kfree(file);
	}
}

int
vfs_lookup(vnode_t *cwd, vnode_t **out, const char *pathname)
{
	vnode_t *vn;
	char path[255], *sub, *next;
	size_t sublen;
	bool last = false;
	int r;

	if (pathname[0] == '/' || cwd == NULL) {
		vn = root_vnode;
		if (*(pathname++) == '\0') {
			*out = vn;
			return 0;
		}
	} else
		vn = cwd;

	strcpy(path, pathname);
	sub = path;

loop:
	sublen = 0;
	next = sub;

	while (*next != '\0' && *next != '/') {
		next++;
		sublen++;
	}

	if (*next == '\0') {
		/* end of path */
		last = true;
	} else
		*next = '\0';

	if (strcmp(sub, ".") == 0)
		; /* do nothing */
	else {
		r = VOP_LOOKUP(vn, &vn, sub);
		if (r < 0) {
			return r;
		}
	}

	if (last) {
		*out = vn;
		return 0;
	}

	sub += sublen + 1;
	goto loop;
}

int
vfs_read(vnode_t *vn, void *buf, size_t nbyte, off_t off)
{
	return VOP_READ(vn, buf, nbyte, off);
}

int
vfs_write(vnode_t *vn, void *buf, size_t nbyte, off_t off)
{
	return VOP_WRITE(vn, buf, nbyte, off);
}

int
sys_open(struct posix_proc *proc, const char *path, int mode)
{
	vnode_t *vn;
	int r;
	int fd = -1;

	kprintf("sys_open(%s,%d)\n", path, mode);

	for (int i = 0; i < countof(proc->files); i++) {
		if (proc->files[i] == NULL) {
			fd = i;
			break;
		}
	}

	if (fd == -1)
		return -ENFILE;

	r = vfs_lookup(root_vnode, &vn, path);
	if (r < 0) {
		kprintf("lookup returned %d\n", r);
		return r;
	}

	if (vn->ops->open) {
		r = vn->ops->open(vn, mode, proc);
		if (r < 0) {
			kprintf("lookup returned %d\n", r);
			return r;
		}
	}

	proc->files[fd] = kmalloc(sizeof(file_t));
	assert(proc->files[fd] != NULL);
	proc->files[fd]->vn = vn;
	proc->files[fd]->refcnt = 1;
	proc->files[fd]->fops = NULL;
	proc->files[fd]->pos = 0;

	kprintf("yielded fd %d\n", fd);
	return fd;
}

int
sys_close(struct posix_proc *proc, int fd, uintptr_t *errp)
{
	file_t *file;

	/* lock proc fdlock */
	file = proc->files[fd];

	if (file == NULL) {
		*errp = EBADF;
		return 0;
	}

	file_unref(file);
	proc->files[fd] = NULL;

	/* todo unlock proc fdlock */

	return 0;
}

int
sys_read(struct posix_proc *proc, int fd, void *buf, size_t nbyte)
{
	file_t *file = proc->files[fd];
	int r;

	kprintf("SYS_READ(%d, nbytes: %lu off: %lu)\n", fd, nbyte, file->pos);

	if (file == NULL)
		return -EBADF;

	r = vfs_read(file->vn, buf, nbyte, file->pos);
	if (r < 0) {
		kprintf("vfs_read got %d\n", r);
		return r;
	}

	file->pos += nbyte;

	return nbyte;
}

int
sys_write(struct posix_proc *proc, int fd, void *buf, size_t nbyte)
{
	file_t *file = proc->files[fd];
	int r;

	kprintf("SYS_WRITE(%d, nbytes: %lu off: %lu)\n", fd, nbyte, file->pos);

	if (file == NULL)
		return -EBADF;

	r = vfs_write(file->vn, buf, nbyte, file->pos);
	if (r < 0) {
		kprintf("vfs_write got %d\n", r);
		return r;
	}

	file->pos += r;

	return r;
}

int
sys_seek(struct posix_proc *proc, int fd, off_t offset, int whence)
{
	file_t *file = proc->files[fd];

	if (file == NULL)
		return -EBADF;

	if (file->vn->type != VREG)
		return -ESPIPE;

	assert(whence == SEEK_SET);

	kprintf("SYS_SEEK(offset: %ld)\n", offset);

	file->pos = offset;
	return offset;
}

int
sys_isatty(struct posix_proc *proc, int fd, uintptr_t *errp)
{
	file_t *file;

	file = proc->files[fd];

	if (file == NULL) {
		*errp = EBADF;
		return 0;
	}

	if (file->vn->type != VCHR || !cdevsw[major(file->vn->dev)].is_tty) {
		*errp = ENOTTY;
		return 0;
	}

	return 1;
}
