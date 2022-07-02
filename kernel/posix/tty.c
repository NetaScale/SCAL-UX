#include <errno.h>
#include <stdbool.h>

#include "kern/kern.h"
#include "kern/waitq.h"
#include "termios.h"
#include "tty.h"

#define ISSET(FIELD, VAL) ((FIELD)&VAL)

/* FIXME: remove */
extern tty_t *sctty;

static bool
isttycanon(tty_t *tty)
{
	return tty->termios.c_lflag & ICANON;
}

static bool
isttyisig(tty_t *tty)
{
	return tty->termios.c_lflag & ISIG;
}

static int
enqueue(tty_t *tty, int c)
{
	if (tty->buflen == sizeof(tty->buf))
		return -1;

	if (c == '\n')
		tty->nlines++;

	tty->buf[tty->writehead++] = c;
	if (tty->writehead == sizeof(tty->buf))
		tty->writehead = 0;
	tty->buflen++;

	if (!isttycanon(tty))
		waitq_wake_one(&tty->wq_noncanon, 0);
	else if (c == '\n')
		waitq_wake_one(&tty->wq_canon, 0);

	return 0;
}

static int
unenqueue(tty_t *tty)
{
	off_t prevwritehead;
	int prevc;

	if (tty->buflen == 0)
		return -1;

	prevwritehead = tty->writehead == 0 ? sizeof(tty->buf - 1) :
					      tty->writehead - 1;

	prevc = tty->buf[prevwritehead];
	/* no erasure after newline */
	if (tty->buf[prevwritehead] == tty->termios.c_cc[VEOL] ||
	    tty->buf[prevwritehead == '\n'])
		return '\0';

	tty->writehead = prevwritehead;
	tty->buflen--;
	return prevc;
}

static int
dequeue(tty_t *tty)
{
	int c;

	if (tty->buflen == 0)
		return '\0';

	c = tty->buf[tty->readhead++];
	if (tty->readhead == sizeof(tty->buf))
		tty->readhead = 0;

	if (c == '\n' || c == tty->termios.c_cc[VEOL])
		tty->nlines--;

	tty->buflen--;
	return c;
}

void
tty_input(tty_t *tty, int c)
{
	/* signals */
	if (isttyisig(tty)) {
		if (c == tty->termios.c_cc[VINTR]) {
			kprintf("VINTR on tty %p\n", tty);
			return;
		} else if (c == tty->termios.c_cc[VQUIT]) {
			kprintf("VQUIT on tty %p\n", tty);
			return;
		} else if (c == tty->termios.c_cc[VSUSP]) {
			kprintf("VSUSP on tty %p\n", tty);
			return;
		}
	}

	/* newline */
	if (c == '\r') {
		if (ISSET(tty->termios.c_iflag, IGNCR))
			return;
		else if (ISSET(tty->termios.c_iflag, ICRNL))
			c = '\n';
	} else if (c == '\n' && ISSET(tty->termios.c_iflag, INLCR))
		c = '\r';

	if (isttycanon(tty)) {
		/* erase ^h/^? */
		if (c == tty->termios.c_cc[VERASE]) {
			unenqueue(tty);
			/* write to output '\b' ' ' '\b' */
			return;
		}

		/* erase word ^W */
		if (c == tty->termios.c_cc[VWERASE] &&
		    ISSET(tty->termios.c_lflag, IEXTEN)) {
			kprintf("VWERASE on tty %p\n", tty);
			return;
		}
	}

	enqueue(tty, c);
	if (tty->termios.c_lflag & ECHO /* and is the code printable? */)
		/* print to the underlying tty too... */
		tty->putch(tty->data, c);
}

int
tty_read(dev_t dev, void *buf, size_t nbyte, off_t off)
{
	size_t nread = 0;
	tty_t *tty = NULL;

	assert(off == 0);

	kprintf("tty_read\n");
	for (;;)
		;

	if (tty->buflen < nbyte)
		nbyte = tty->buflen;

	if (isttycanon(tty) && !tty->nlines) {
		waitq_await(&tty->wq_canon, 1, 25000);
	}

	while (nread < nbyte) {
		int c = dequeue(tty);
		((char *)buf)[nread++] = c;
		if (c == '\n' || c == tty->termios.c_cc[VEOL])
			break;
	}

	return nread;
}

int
tty_write(dev_t dev, void *buf, size_t nbyte, off_t off)
{
	tty_t *tty = NULL;
	for (int i = 0; i < nbyte; i++) {
		sysconputc(((char *)buf)[i]);
#if 0
		void limterm_putc(int ch, void *ctx);
		limterm_putc(((char *)buf)[i], NULL);
#endif
	}
	sysconflush();
	return nbyte;
}

int
tty_select(dev_t dev, waitq_t *wq)
{
	tty_t *tty = sctty;

	kprintf("tty select\n");

	return 0;
}
