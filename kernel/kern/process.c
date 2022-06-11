#include "process.h"


struct allprocs allprocs = TAILQ_HEAD_INITIALIZER(allprocs);
spinlock_t process_lock;
process_t proc0 = {
	.pid = 0,
	.name = "[kernel]"
};

void setup_proc0()
{
	proc0.map = kmap;
	TAILQ_INSERT_HEAD(&allprocs, &proc0, allprocs);
}