/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#ifndef KERN_TYPES_H_
#define KERN_TYPES_H_

#include <sys/types.h>

typedef void * vaddr_t, *paddr_t;
typedef size_t voff_t, pgoff_t;

#define NS_PER_S 1000000000

/* does not belong here */
struct msgbuf {
	char buf[4096];
	size_t read, write;
};

extern struct msgbuf msgbuf;

#endif /* KERN_TYPES_H_ */
