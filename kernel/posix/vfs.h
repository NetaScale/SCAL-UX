#ifndef VFS_H_
#define VFS_H_

#include <sys/types.h>

#include <signal.h>

#include "kern/vm.h"

struct knote;
struct posix_proc;
typedef enum vtype { VNON, VREG, VDIR, VCHR } vtype_t;
typedef struct vnode vnode_t;
typedef struct file file_t;

struct vnops {
	/**
	 * Create a new VREG file vnode in the given directory.
	 *
	 * @param dvn LOCKED directory vnode
	 * @param out [out] resultant vnode (add a ref for caller)
	 * @param name new file name
	 */
	int (*create)(vnode_t *dvn, vnode_t **out, const char *name);

	/**
	 * Allocate backing store.
	 *
	 * @param dvn LOCKED regular file vnode
	 * @param off offset at which to allocate
	 * @param len length in bytes to allocate
	 */
	int (*fallocate)(vnode_t *vn, off_t off, size_t len);

	/**
	 * Lookup the vnode corresponding to the given file name in the given
	 * direct vnode.
	 *
	 * @param dvn LOCKED directory vnode
	 * @param out [out] resultant vnode (add a ref for caller)
	 * @param name filename
	 */
	int (*lookup)(vnode_t *dvn, vnode_t **out, const char *name);

	/**
	 * Create a new directory in the given directory.
	 *
	 * @param dvn LOCKED directory vnode
	 * @param out [out] resultant vnode (add a ref for caller)
	 * @param name filename
	 */
	int (*mkdir)(vnode_t *dvn, vnode_t **out, const char *name);

	/**
	 * Create a new directory in the given directory.
	 *
	 * @param dvn LOCKED directory vnode
	 * @param out [out] resultant vnode (add a ref for caller)
	 * @param name filename
	 */
	int (*mknod)(vnode_t *dvn, vnode_t **out, const char *name, dev_t dev);

	/**
	 * Get a page from a VREG vnode's backing store or cache. Overridable so
	 * that tmpfs can avoid having two sets of pages around.
	 *
	 * @param dvn LOCKED directory vnode
	 * @param off offset in bytes into the vnode (will be multiple of
	 * PGSIZE)
	 * @param out [out] resultant anonymous page
	 * @param needcopy whether this is for a copy-on-write mapping (i.e.
	 * the anon should not be from or entered into the cache as it'll be
	 * written to forthwith).
	 */
	int (*getpage)(vnode_t *dvn, voff_t off, vm_anon_t **out,
	    bool needscopy);

	/* todo: putpages */

	int (*open)(vnode_t *vn, int mode, struct posix_proc *proc);

	int (*read)(vnode_t *vn, void *buf, size_t nbyte, off_t off);

	int (*write)(vnode_t *vn, void *buf, size_t nbyte, off_t off);

	int (*kqfilter)(vnode_t *vn, struct knote *kn);
};

typedef struct vattr {
	size_t size;
} vattr_t;

typedef struct vnode {
	size_t refcnt;
	vtype_t type;
	vm_object_t *vmobj; /* page cache */
	void *data;	    /* fs-private data */
	struct vnops *ops;
	vattr_t attr;
	dev_t dev;
	spinlock_t interlock;
} vnode_t;

/**
 * Represents a mounted filesystem.
 */
typedef struct vfs {
	void *data; /* fs-private data */
} vfs_t;

typedef struct fileops {
	/** Read \p nbyte at offset \p off into buffer \p buf */
	int (*read)(file_t *file, void *buf, size_t nbyte, off_t off);
	/** Write \p nbyte at offset \p off into buffer \p buf */
	int (*write)(file_t *file, void *buf, size_t nbyte, off_t off);
	/** Yield a (refcount-incremented) VM object which can map this file. */
	int (*mmap)(file_t *file, off_t off, size_t len, bool copy,
	    vm_object_t **out);
} fileops_t;

/*
 * Kernel file descriptor, called on Linux a 'file description'.
 */
typedef struct file {
	size_t refcnt;
	fileops_t *fops;
	vnode_t *vn;
	size_t pos;
} file_t;

enum lookup_flags {
	kLookupCreat = 1,
	kLookupMkdir = 2,
	kLookupMknod = 4,
	kLookupMustDir = 8,
};

void tmpfs_mountroot();

/**
 * Lookup path \p path relative to @locked \p cwd and store the result in
 * \p out. Refcount of the vnode is incremented.
 * If \p last2 is set, then returns the second-to-last component of the path.
 */
int vfs_lookup(vnode_t *cwd, vnode_t **out, const char *path, int flags);

/**
 * Read from @locked \p vn \p nbyte bytes at offset \p off into buffer \p buf.
 */
int vfs_read(vnode_t *vn, void *buf, size_t nbyte, off_t off);

/**
 * Read into @locked \p vn \p nbyte bytes at offset \p off from buffer \p buf.
 */
int vfs_write(vnode_t *vn, void *buf, size_t nbyte, off_t off);

/* open a file; returns fd */
int sys_open(struct posix_proc *proc, const char *path, int mode);
int sys_close(struct posix_proc *proc, int fd, uintptr_t *errp);
int sys_read(struct posix_proc *proc, int fd, void *buf, size_t nbyte);
int sys_write(struct posix_proc *proc, int fd, void *buf, size_t nbyte);
int sys_seek(struct posix_proc *proc, int fd, off_t offset, int whence);
int sys_pselect(struct posix_proc *proc, int nfds, fd_set *readfds,
    fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout,
    const sigset_t *sigmask, uintptr_t *errp);
int sys_isatty(struct posix_proc *proc, int fd, uintptr_t *errp);

extern vnode_t *root_vnode;
extern vnode_t *root_dev;

#endif /* VFS_H_ */
