#ifndef PROC_H_
#define PROC_H_

#include "vm.h"

typedef struct task {
	bool kern; /* is it a kernel task? */

	vm_map_t * map;
} task_t;

#endif /* PROC_H_ */
