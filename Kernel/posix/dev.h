#ifndef DEV_H_
#define DEV_H_

#include <sys/types.h>

#include <stdbool.h>

struct knote;
struct proc;

typedef struct cdevsw {
	bool valid : 1, is_tty : 1;
	void *private;
	int (*open)(dev_t dev, int mode, struct proc *proc);
	int (*read)(dev_t dev, void *buf, size_t nbyte, off_t off);
	int (*write)(dev_t dev, void *buf, size_t nbyte, off_t off);
	int (*kqfilter)(dev_t dev, struct knote *kn);
} cdevsw_t;

/* Attach an entry to the  device switch; its major number is returned. */
int cdevsw_attach(cdevsw_t *bindings);

extern cdevsw_t cdevsw[64];

#endif /* DEV_H_ */
