/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <sys/select.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include "abi-bits/seek-whence.h"
#include "dev.h"
#include "event.h"
#include "kern/buildconf.h"
#include "libkern/klib.h"
#include "proc.h"
#include "vfs.h"

vnode_t *root_vnode = NULL;
vnode_t *root_dev = NULL;

#define VOP_READ(vnode, buf, nbyte, off) \
	vnode->ops->read(vnode, buf, nbyte, off)
#define VOP_WRITE(vnode, buf, nbyte, off) \
	vnode->ops->write(vnode, buf, nbyte, off)
#define VOP_CREAT(vnode, out, name, attr) \
	vnode->ops->create(vnode, out, name, attr)
#define VOP_LOOKUP(vnode, out, path) vnode->ops->lookup(vnode, out, path)
#define VOP_MKDIR(vnode, out, name, attr) \
	vnode->ops->mkdir(vnode, out, name, attr)

#define countof(ARRAY) (sizeof(ARRAY) / sizeof(ARRAY[0]))

void
FD_CLR(int fd, fd_set *set)
{
	set->__mlibc_elems[fd / 8] &= ~(1 << (fd % 8));
}
int
FD_ISSET(int fd, fd_set *set)
{
	return set->__mlibc_elems[fd / 8] & (1 << (fd % 8));
}
void
FD_SET(int fd, fd_set *set)
{
	set->__mlibc_elems[fd / 8] |= 1 << (fd % 8);
}
void
FD_ZERO(fd_set *set)
{
	memset(set->__mlibc_elems, 0, sizeof(fd_set));
}

static void
file_unref(file_t *file)
{
	if (--file->refcnt == 0) {
		// vn_unref(file->vn);
		file->magic = 0xDEADF11E;
		kfree(file);
	}
}

static vnode_t *
reduce(vnode_t *vn)
{
	return vn;
}

int
vfs_lookup(vnode_t *cwd, vnode_t **out, const char *pathname, int flags,
    vattr_t *attr)
{
	vnode_t *vn, *prevvn = NULL;
	char	 path[255], *sub, *next;
	size_t	 sublen;
	bool	 last = false;
	bool	 mustdir = flags & kLookupMustDir;
	size_t	 len = strlen(pathname);
	int	 r;

	if (pathname[0] == '/' || cwd == NULL) {
		vn = root_vnode;
		if (*(pathname + 1) == '\0') {
			*out = vn;
			return 0;
		}
	} else
		vn = cwd;

	strcpy(path, pathname);
	sub = path;

	if (path[len - 1] == '/') {
		size_t last = len - 1;
		while (path[last] == '/')
			path[last--] = '\0';
		mustdir = true;
		if (*path == '\0') {
			*out = vn;
			return 0;
		}
	}

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

	if (strcmp(sub, ".") == 0 || sublen == 0)
		goto next; /* . or trailing */

	prevvn = vn;

	if (!last ||
	    (!(flags & kLookupMkdir) && !(flags & kLookupCreat) &&
		!(flags & kLookupMknod))) {
		// kprintf("lookup %s in %p\n", sub, vn);
		r = VOP_LOOKUP(vn, &vn, sub);
	} else if (flags & kLookupMkdir)
		r = VOP_MKDIR(vn, &vn, sub, attr);
	else if (flags & kLookupCreat)
		r = VOP_CREAT(vn, &vn, sub, attr);

	if (prevvn != vn)
		// vn_unref(vn); TODO:
		;
	if (r < 0) {
		// vn_unref(vn);
		return r;
	}

next:
	if (last)
		goto out;

	// kprintf("sub %s => %p\n", sub, vn);

	sub += sublen + 1;
	goto loop;

out:
	if (mustdir)
		vn = reduce(vn);
	*out = vn;
	return 0;
}

int
vfs_read(vnode_t *vn, void *buf, size_t nbyte, off_t off)
{
	return VOP_READ(vn, buf, nbyte, off);
}

int
vfs_write(vnode_t *vn, void *buf, size_t nbyte, off_t off)
{
	assert(vn && vn->ops && vn->ops->write);
	return VOP_WRITE(vn, buf, nbyte, off);
}

int
sys_open(struct proc *proc, const char *path, int mode)
{
	vnode_t *vn;
	int	 r;
	int	 fd = -1;

	for (int i = 0; i < countof(proc->files); i++) {
		if (proc->files[i] == NULL) {
			fd = i;
			break;
		}
	}

#if DEBUG_SYSCALLS == 1
	kprintf("PID %d sys_open(%s,%d) to FD %d\n", proc->task->pid, path,
	    mode, fd);
#endif

	if (fd == -1)
		return -ENFILE;

	r = vfs_lookup(root_vnode, &vn, path, 0, NULL);
	if (r < 0 && mode & kLookupCreat)
		r = vfs_lookup(root_vnode, &vn, path, kLookupCreat, NULL);

	if (r < 0) {
#if DEBUG_SYSCALLS == 1
		kprintf("lookup returned %d\n", r);
#endif
		return r;
	}

	if (vn->ops->open) {
		r = vn->ops->open(vn, mode, proc);
		if (r < 0) {
#if DEBUG_SYSCALLS == 1
			kprintf("open returned %d\n", r);
#endif
			return r;
		}
	}

	proc->files[fd] = kmalloc(sizeof(file_t));
	assert(proc->files[fd] != NULL);
	proc->files[fd]->vn = vn;
	proc->files[fd]->refcnt = 1;
	proc->files[fd]->magic = 0x112EF11E;
	proc->files[fd]->fops = NULL;
	proc->files[fd]->pos = 0;

	return fd;
}

int
sys_close(struct proc *proc, int fd, uintptr_t *errp)
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
sys_read(struct proc *proc, int fd, void *buf, size_t nbyte)
{
	file_t *file = proc->files[fd];
	int	r;

#if DEBUG_SYSCALLS == 1
	kprintf("SYS_READ(%d, nbytes: %lu off: %lu)\n", fd, nbyte, file->pos);
#endif

	if (file == NULL)
		return -EBADF;

	r = vfs_read(file->vn, buf, nbyte, file->pos);
	if (r < 0) {
		kprintf("vfs_read got %d\n", r);
		return r;
	}

	file->pos += r;

	return r;
}

int
sys_write(struct proc *proc, int fd, void *buf, size_t nbyte)
{
	file_t *file = proc->files[fd];
	int	r;

#if DEBUG_SYSCALLS == 1
	kprintf("PID %d SYS_WRITE(%d, nbytes: %lu off: %lu)\n", proc->task->pid,
	    fd, nbyte, file->pos);
#endif

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
sys_seek(struct proc *proc, int fd, off_t offset, int whence)
{
	file_t *file = proc->files[fd];

#if DEBUG_SYSCALLS == 1
	kprintf("SYS_SEEK(offset: %ld)\n", offset);
#endif

	if (file == NULL)
		return -EBADF;

	if (file->vn->type != VREG)
		return -ESPIPE;

	if (whence == SEEK_SET)
		file->pos = offset;
	else if (whence == SEEK_CUR)
		file->pos += offset;
	else if (whence == SEEK_END) {
		vattr_t attr;
		int	r;

		r = file->vn->ops->getattr(file->vn, &attr);
		if (r < 0)
			return -1;

		file->pos = attr.size + offset;
	}

	return file->pos;
}

int
sys_pselect(struct proc *proc, int nfds, fd_set *readfds, fd_set *writefds,
    fd_set *exceptfds, const struct timespec *timeout, const sigset_t *sigmask,
    uintptr_t *errp)
{
	kqueue_t *kq = kqueue_new();
	int	  r;

#if DEBUG_SYSCALLS == 1
	kprintf("SYS_PSELECT\n");
#endif

	for (int i = 0; i < nfds; i++) {
		int	      filt = 0;
		struct kevent kev;

		if (FD_ISSET(i, readfds))
			filt = EVFILT_READ;

		if (filt == 0)
			continue;

		EV_SET(&kev, i, filt, EV_ADD, 0, 0, NULL);
		kqueue_register(kq, &kev);
	}

	r = kqueue_wait(kq, 0);
	return r == kWaitQResultTimeout ? 0 : 1;
}

int
sys_isatty(struct proc *proc, int fd, uintptr_t *errp)
{
	file_t *file;

	file = proc->files[fd];

#if 0 // DEBUG_SYSCALLS == 1
	kprintf("SYS_ISATTY(%d/type %d/cdevsw %d)\n", fd, file->vn->type,
	    cdevsw[major(file->vn->specdev->dev)].is_tty);
#endif

	if (file == NULL) {
		*errp = EBADF;
		return -1;
	}

	if (file->magic != FILEMAGIC) {
		fatal("unexpected file magic\n");
	}

	if (file->vn->type != VCHR ||
	    !cdevsw[major(file->vn->specdev->dev)].is_tty) {
		*errp = ENOTTY;
		return -1;
	}

	return 1;
}

int
sys_readdir(struct proc *proc, int fd, void *buf, size_t nbyte,
    size_t *bytesRead, uintptr_t *errp)
{
	file_t *file;
	int	r;

#if DEBUG_SYSCALLS == 1
	kprintf("SYS_READDIR\n");
#endif

	file = proc->files[fd];
	if (file == NULL || file->vn->type != VDIR) {
		*errp = EBADF;
		return 0;
	}

	r = file->vn->ops->readdir(file->vn, buf, nbyte, bytesRead, file->pos);
	if (r < 0) {
		*errp = -r;
		return -1;
	}

	file->pos = r;

	return 0;
}

int
sys_stat(struct proc *proc, int fd, const char *path, int flags,
    struct stat *out, uintptr_t *errp)
{
	int	 r = 0;
	vnode_t *vn;
	vattr_t	 vattr = { 0 };

#if DEBUG_SYSCALLS == 1
	kprintf("SYS_STAT(%d, %s, %d, %p)\n", fd, path ? path : "<null>", flags,
	    out);
#endif

	if (fd == AT_FDCWD) {
		r = vfs_lookup(root_vnode, &vn, path, 0, NULL);
		if (r < 0) {
			*errp = -r;
			r = -1;
			goto finish;
		}
	} else {
		file_t *file;

		assert(fd >= 0 && fd < 64) file = proc->files[fd];

		if (path && strlen(path) != 0) {
			r = vfs_lookup(file->vn, &vn, path, 0, NULL);

			if (r < 0) {
				*errp = -r;
				r = -1;
				goto finish;
			}
		} else {
			vn = file->vn;
		}
	}

	if (vn->ops->getattr)
		r = vn->ops->getattr(vn, &vattr);
	if (r < 0) {
		*errp = -r;
		r = -1;
		goto finish;
	}

	memset(out, 0x0, sizeof *out);

	out->st_mode = vattr.mode;

	switch (vattr.type) {
	case VREG:
		out->st_mode |= S_IFREG;
		break;

	case VDIR:
		out->st_mode |= S_IFDIR;
		break;

	case VCHR:
		out->st_mode |= S_IFCHR;
		out->st_rdev = vn->specdev->dev;
		break;

	default:
		/* nothing */
		kprintf("sys_stat: unexpected vattr type %d\n", vattr.type);
		break;
	}

	out->st_size = vattr.size;
	out->st_blocks = vattr.size / 512;
	out->st_blksize = 512;

finish:
	return r;
}
