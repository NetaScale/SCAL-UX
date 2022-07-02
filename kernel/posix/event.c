#include <stddef.h>

#include "event.h"
#include "kern/liballoc.h"
#include "kern/queue.h"
#include "posix/posix_proc.h"
#include "vfs.h"

kqueue_t *
kqueue_new()
{
	kqueue_t *kq = kmalloc(sizeof *kq);
	TAILQ_INIT(&kq->knotes);
	waitq_init(&kq->wq);
	kq->sleeping = false;
	return kq;
}

int
kqueue_register(kqueue_t *kq, struct kevent *kev)
{
	knote_t *kn = kmalloc(sizeof *kn);

	assert(kn);
	kn->status = 0;
	kn->kev = *kev;
	kn->kq = kq;

	TAILQ_INSERT_TAIL(&kq->knotes, kn, entries);

	switch (kn->kev.filter) {
	case EVFILT_READ: {
		file_t *file = CURPXPROC()->files[kev->ident];
		file->vn->ops->kqfilter(file->vn, kn);
		break;
	}

	default:
		fatal("unsupported kevent filter %d\n", kev->filter);
	}

	return 0;
}

int
kqueue_wait(kqueue_t *kq)
{
	int r;
	kq->sleeping = true;
	r =  waitq_await(&kq->wq, 1, 250000);
	kq->sleeping = false;
	return r;
}

int
knote_notify(knote_t *kn)
{
	if (kn->kq->sleeping)
		waitq_wake_one(&kn->kq->wq, 1);

	return 0;
}
