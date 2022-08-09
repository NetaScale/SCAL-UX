/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

/**
 * @file task.h
 * @brief Tasks and threads.
 */


#ifndef TASK_H_
#define TASK_H_

#include <kern/vm.h>
#include <machine/machdep.h>

typedef struct task {
        char name[31];
        vm_map_t *map;
} task_t;

typedef struct thread {
        /*! task to which the thread belongs */
        task_t *task;

        /*! machine-dependent thread block */
        md_thread_t md;
} thread_t;

typedef struct cpu {
        int num;
        thread_t  *curthread;

        /*! machine-dependent cpu block */
        md_cpu_t md;
} cpu_t;

static inline thread_t *curthread()
{
        return curcpu()->curthread;
}

static inline task_t *curtask()
{
        return curcpu()->curthread->task;
}

extern task_t task0;
extern thread_t thread0;
extern cpu_t cpu0;

#endif /* TASK_H_ */
