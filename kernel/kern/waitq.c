#include "amd64.h"
#include "kern/queue.h"
#include "process.h"
#include "spl.h"
#include "waitq.h"

/* callout function for an expired waitq timeout */
static void
waitq_timeout(void *arg)
{
	thread_t *thread = (thread_t *)arg;
	lock(&thread->lock);
	waitq_lock(thread->wq);
	waitq_clear_locked(thread, kWaitQResultTimeout);
	waitq_unlock(thread->wq);
        unlock(&thread->lock);
        thread_run(thread);
}

void
waitq_init(waitq_t *wq)
{
	wq->lock = 0;
	TAILQ_INIT(&wq->waiters);
}

void waitq_clear_locked(struct thread * thread, waitq_result_t res)
{
	TAILQ_REMOVE(&thread->wq->waiters, &thread->wqent, entries);
	thread->wq = NULL;
	thread->wqev = 0x0;
	thread->wqres = kWaitQResultTimeout;
}

uint64_t
waitq_await(waitq_t *wq, waitq_event_t ev, uint64_t msecs)
{
	spl_t spl;
	thread_t *thread = CURCPU()->curthread;

	splassertle(kSPL0);

	thread->wqent.thread = thread;
	thread->wqtimeout.fun = waitq_timeout;
	thread->wqtimeout.arg = thread;
	thread->wqtimeout.timeout = msecs;

	spl = splhigh();
	waitq_lock(wq);
	lock(&thread->lock);
	TAILQ_INSERT_TAIL(&wq->waiters, &thread->wqent, entries);
	thread->wq = wq;
	thread->wqev = ev;
	thread->wqres = kWaitQResultWaiting;
	waitq_unlock(wq);
	splx(spl);

	return thread_block_locked();
}

/**
 * @returns a locked thread or NULL
 * \pre wq locked; SPL soft
 */
static thread_t *
waitq_get_first(waitq_t *wq, waitq_event_t ev)
{
	waitq_entry_t *ent;
	thread_t *thrd = NULL;

	TAILQ_FOREACH (ent, &wq->waiters, entries) {
		if (ent->type == kWaitQEntrySubqueue) {
			waitq_lock(ent->subqueue);
			thrd = waitq_get_first(ent->subqueue, ev);
			waitq_unlock(ent->subqueue);
			if (thrd != NULL)
				return thrd;
		} else {
			thrd = ent->thread;
			lock(&thrd->lock);
			TAILQ_REMOVE(&wq->waiters, ent, entries);
			thrd->wq = NULL;
			thrd->wqev = 0x0;
			callout_dequeue(&thrd->wqtimeout);
			break;
		}
	}

	return thrd;
}

/**
 * \pre SPL soft
 */
void
waitq_wake_one(waitq_t *wq, waitq_event_t ev)
{
	thread_t *thrd;
	spl_t spl = splsoft();

	waitq_lock(wq);
	thrd = waitq_get_first(wq, ev);
	waitq_unlock(wq);

	if (!thrd) {
		kprintf("warning: waitq %p sent event %lu with no waiters\n",
		    wq, ev);
		return;
	}

	thrd->wqres = kWaitQResultEvent;
	unlock(&thrd->lock);
	thread_run(thrd);
	splx(spl);
}
