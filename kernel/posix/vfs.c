#include <string.h>

#include "vfs.h"

vnode_t *root_vnode = NULL;

#define VOP_LOOKUP(vnode, out, path) vnode->ops->lookup(vnode, out, path)

int
lookup(vnode_t *cwd, vnode_t **out, const char *pathname)
{
	vnode_t *vn;
	char path[255], *sub;
	size_t sublen;
	int r;

	if (pathname[0] == '/' || cwd == NULL) {
		vn = root_vnode;
		if (*(pathname++) == '\0') {
			*out = vn;
			return 0;
		}
	}

	strcpy(path, pathname);
	sub = path;

loop:
	sublen = 0;

	while (*sub != '\0' && *sub != '/') {
		sub++;
		sublen++;
	}

	if (*sub == '\0') {
		/* end of path */
		*out = vn;
		return 0;
	} else
		*sub = '\0';

	if (strcmp(sub, ".") == 0)
		; /* do nothing */
	else {
		r = VOP_LOOKUP(vn, &vn, sub);
		if (r < 0) {
			return r;
		}
	}

	sub += sublen + 1;
	goto loop;
}