#include "libkern/klib.h"
#include "lock.h"
#include "task.h"

void
mutex_init(mutex_t *mtx)
{
	waitq_init(&mtx->waitq);
	mtx->holder = NULL;
	mtx->count = 0;
}

void
mutex_lock(mutex_t *mtx)
{
	assert(mtx->holder != CURTHREAD());
	if (++mtx->count > 1) {
		//kprintf("THREAD %p SLEEPING ON MUTEX HELD BY %p...\n\n\n", CURTHREAD(), mtx->holder);
		//for (;; ) ;
		waitq_await(&mtx->waitq, 0, 0);
	}
	//kprintf("%p locking mutex %p\n", __builtin_return_address(0), mtx);
	mtx->holder = CURTHREAD();
}

void
mutex_unlock(mutex_t *mtx)
{
	assert(mtx->holder == CURTHREAD());
	mtx->holder = NULL;
	//	kprintf("%p UNlocking mutex %p\n", __builtin_return_address(0), mtx);

	if (--mtx->count > 0) {
		waitq_wake_one(&mtx->waitq, 0);
	}
}
