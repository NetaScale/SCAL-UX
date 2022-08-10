/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <machine/machdep.h>
#include <kern/task.h>

void md_switch(struct thread *from, struct thread *to)
{
        curcpu()->md.old = from;
        asm("int $240");
}
