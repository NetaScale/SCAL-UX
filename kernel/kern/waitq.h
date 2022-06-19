#ifndef WAITQ_H_
#define WAITQ_H_

#include "klock.h"
#include "kern/queue.h"
#include "spl.h"

typedef uintptr_t waitq_event_t;

typedef enum waitq_result {
        kWaitQResultWaiting = -1,
        kWaitQResultTimeout,
        kWaitQResultInterrupted,
        kWaitQResultEvent,
} waitq_result_t;

/**
 * An entry in a wait queue. May either be a thread to be woken when the waitq
 * is signalled, or a subqueue to be signalled in turn. Embedded in the object
 * which will do the waiting.
 */
typedef struct waitq_entry {
        enum {
                kWaitQEntrySubqueue,
                kWaitQEntryThread,
        } type;
        union {
                struct waitq *subqueue;
                struct thread *thread;
        };
        TAILQ_ENTRY(waitq_entry) entries;
} waitq_entry_t;

/**
 * A wait queue. Embedded in an object which may be waited on.
 */
typedef struct waitq {
        spinlock_t lock;
        TAILQ_HEAD(, waitq_entry) waiters;
        spl_t oldspl;
} waitq_t;


/** Lock a waitq. */
static inline void waitq_lock(waitq_t *wq)
{
        wq->oldspl = splhigh();
        return lock(&wq->lock);
}

/** Unlock a waitq. */
static inline void waitq_unlock(waitq_t *wq)
{
        unlock(&wq->lock);
        splx(wq->oldspl);
}

/**
 * Initialize an embedded waitq.
 */
void waitq_init(waitq_t *wq);

/**
 * Wait on a waitq for the given event for up to \p msecs milliseconds.
 *
 * @returns event number if an event was triggered
 * @returns 0 if timeout elapsed
 *
 * \pre SPL < soft (to allow rescheduling)
 */
uint64_t waitq_await(waitq_t *wq, waitq_event_t ev, uint64_t msecs);

/**
 * Clear waiting early for a given thread.
 *
 * \pre \p thread is locked; \p thread->wq is locked; SPL <= soft */
void waitq_clear_locked(struct thread * thread, waitq_result_t res);

/**
 * Wake one waiter on a waitq.
 */
void waitq_wake_one(waitq_t *wq, waitq_event_t ev);


#endif /* WAITQ_H_ */
