#ifndef TTY_H_
#define TTY_H_

#include <sys/types.h>
#include <sys/termios.h>

#include <stddef.h>

struct file;
struct proc;

typedef struct tty {
	struct termios termios; /* termios */
	char buf[2048];		/* input buffer */
	size_t buflen;		/* input buffer current length */
	off_t readhead;		/* input buffer read head */
	off_t writehead;	/* input buffer write head */
	size_t nlines; /* number of lines available to read in input buffer */
} tty_t;

/** Supply input to a TTY. */
void tty_input(tty_t *tty, int ch);

int tty_open(dev_t dev, int flag, int mode, struct proc *proc);
int tty_read(struct file *file, void *buf, size_t nbyte, off_t off);
int tty_write(struct file *file, void *buf, size_t nbyte, off_t off);

#endif /* TTY_H_ */
