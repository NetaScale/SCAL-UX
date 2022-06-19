#include "process.h"

waitq_result_t thread_block_locked()
{
        thread_t * thread = CURCPU()->curthread;
        spl_t spl;
        
        splassertle(kSPL0);
        spl = splhigh();
        TAILQ_INSERT_TAIL(&CURCPU()->waitqueue, thread, runqueue);
        thread->state = kWaiting;
        callout_enqueue(&thread->wqtimeout);
        unlock(&thread->lock);
        CURCPU()->timeslice = 0;
        splx(spl);
        asm("int $32");
        kprintf("...\n");
        return CURCPU()->curthread->wqres;
}
