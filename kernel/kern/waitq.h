#ifndef WAITQ_H_
#define WAITQ_H_

#include "klock.h"
#include "kern/queue.h"
#include "process.h"
#include "spl.h"

typedef uintptr_t waitq_event_t;

/**
 * An entry in a wait queue. May either be a thread to be woken when the waitq
 * is signalled, or a subqueue to be signalled in turn.
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
        TAILQ_HEAD(, waitq_entry) entries;
        spl_t oldspl;
        callout_t timeout;
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
 * Wake one waiter on a waitq.
 */
void waitq_wake_one_locked(waitq_t *wq, waitq_event_t ev);

/**
 * Wait on a waitq for the given event for up to \p msecs milliseconds.
 *
 * @returns event number if an event was triggered
 * @returns 0 if timeout elapsed
 */
uint64_t waitq_await_locked(waitq_t *wq, waitq_event_t ev, uint64_t msecs);


#endif /* WAITQ_H_ */
