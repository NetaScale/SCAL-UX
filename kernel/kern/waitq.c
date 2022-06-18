#include "waitq.h"
#include "process.h"


uint64_t waitq_await_locked(waitq_t *wq, waitq_event_t ev, uint64_t msecs)
{
        callout_enqueue(&wq->timeout);
}
