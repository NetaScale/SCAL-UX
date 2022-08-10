/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <kern/task.h>
#include <kern/kmem.h>

task_t task0  = {
        .name ="[kernel]",
        .map = &kmap
};

thread_t thread0 = {
        .task = &task0
};

cpu_t cpu0 = {
        .num = 0,
        .curthread = &thread0
};

void task_init(void)
{

}
