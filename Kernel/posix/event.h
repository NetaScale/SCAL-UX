/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#ifndef EVENT_H_
#define EVENT_H_

#include <sys/types.h>

#include "kern/task.h"
#include "stdbool.h"
#include "sys/queue.h"

struct kevent {
	uintptr_t      ident;  /** filter-unique identifier */
	short	       filter; /** event filter */
	unsigned short flags;  /** action flags; the EV_* macros */
	unsigned int   fflags; /** filter-specific flags */
	intptr_t       data;   /** filter-specific data */
	void	     *udata;  /** opaque user data */
};

#define EV_SET(KEVP, IDENT, FILTER, FLAGS, FFLAGS, DATA, UDATA) \
	do {                                                    \
		struct kevent *kevp = KEVP;                     \
		kevp->ident = (IDENT);                          \
		kevp->filter = (FILTER);                        \
		kevp->flags = (FLAGS);                          \
		kevp->fflags = (FFLAGS);                        \
		kevp->data = (DATA);                            \
		kevp->udata = (UDATA);                          \
	} while (0)

#define EVFILT_READ -1
#define EVFILT_WRITE -2

#define EV_ADD 0x1 /** add to kqueue (enables implicitly) */

#ifdef _KERNEL
typedef struct knote {
	TAILQ_ENTRY(knote) entries;
	SLIST_ENTRY(knote) list; /** for the monitored object's list of notes */

	int	       status;
	struct kevent  kev;
	struct kqueue *kq; /** kqueue to which it belongs */
} knote_t;

typedef struct kqueue {
	TAILQ_HEAD(, knote) knotes;

	bool	sleeping; /** whether the kqueue is being slept on */
	waitq_t wq;	  /** if sleeping, its waitq */
} kqueue_t;

/** create and initialise a new empty kqueue */
kqueue_t *kqueue_new();
/** wait on a kqueue */
int kqueue_wait(kqueue_t *kq);
/** register a single kevent with a kqueue */
int kqueue_register(kqueue_t *kq, struct kevent *kev);
/** notify knote condition change */
int knote_notify(knote_t *kn);

#endif
#endif /* EVENT_H_ */
