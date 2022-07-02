#ifndef DEV_H_
#define DEV_H_

#include <sys/types.h>

#include <stdbool.h>

struct posix_proc;
struct waitq;

typedef struct cdevsw {
	bool valid : 1, is_tty : 1;
	void *private;
	int (*open)(dev_t dev, int mode, struct posix_proc *proc);
	int (*write)(dev_t dev, void *buf, size_t nbyte, off_t off);
	int (*select)(dev_t dev, struct waitq *wq);
} cdevsw_t;

/* Attach an entry to the  device switch; its major number is returned. */
int cdevsw_attach(cdevsw_t *bindings);

extern cdevsw_t cdevsw[64];

#endif /* DEV_H_ */
