#include "amd64.h"
#include "kern/waitq.h"
#include "posix_proc.h"

int
sys_exit(posix_proc_t *proc, int code)
{
	spl_t spl = splsoft();
	thread_t *curthread, *thread;

	curthread = CURCPU()->curthread;

	LIST_FOREACH (thread, &proc->proc->threads, threads) {
		lock(&thread->lock);
		if (thread == curthread)
			continue;
		thread->should_exit = true;
		if (thread->state == kWaiting) {
			waitq_lock(thread->wq);
			waitq_clear_locked(thread, kWaitQResultInterrupted);
			waitq_unlock(thread->wq);
		} else if (thread->state == kRunning && !thread->in_syscall) {
			/* do an IPI or something if it's nonlocal */
			thread->should_exit = true;
			thread->state = kExiting;
		}
		unlock(&thread->lock);
	}

	curthread->should_exit = true;
	unlock(&curthread->lock);

	splx(spl);
	return 0;
}
