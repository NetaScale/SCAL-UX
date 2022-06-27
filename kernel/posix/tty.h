#ifndef TTY_H_
#define TTY_H_

#include <sys/types.h>
#include <sys/termios.h>

#include <stddef.h>

struct file;
struct posix_proc;

typedef struct tty {
	struct termios termios; /* termios */
	char buf[2048];		/* input buffer */
	size_t buflen;		/* input buffer current length */
	off_t readhead;		/* input buffer read head */
	off_t writehead;	/* input buffer write head */
	size_t nlines; /* number of lines available to read in input buffer */
	void *data;
	int (*putch)(void *data, int c);
} tty_t;

#define	TTYDEF_IFLAG	(BRKINT	| ICRNL	| IMAXBEL | IXON | IXANY)
#define TTYDEF_OFLAG	(OPOST | ONLCR)
#define TTYDEF_LFLAG	(ECHO | ICANON | ISIG | IEXTEN | ECHOE|ECHOKE|ECHOCTL)
#define	TTYDEF_CFLAG	(CREAD | CS8 | HUPCL)
#define TTYDEF_SPEED	(B9600)

/* called from a particular device's own open function */
int tty_open(dev_t dev, int mode, struct posix_proc *proc);

/* used in the cdev switch */
int tty_read(dev_t, void *buf, size_t nbyte, off_t off);
int tty_write(dev_t, void *buf, size_t nbyte, off_t off);

/** Supply input to a TTY. */
void tty_input(tty_t *tty, int ch);

#endif /* TTY_H_ */
