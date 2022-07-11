/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#ifndef VM_POSIX_H_
#define VM_POSIX_H_

#include <sys/mman.h>

struct proc;

int vm_mmap(struct proc *proc, void **addr, size_t len, int prot, int flags,
    int fd, off_t offset);

#endif /* VM_POSIX_H_ */
