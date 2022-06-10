#ifndef PROCESS_H_
#define PROCESS_H_

#include "kern/vm.h"

struct process {
	vm_map_t map;
} process_t;

#endif /* PROCESS_H_ */
