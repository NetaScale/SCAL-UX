#ifndef LOCK_H_
#define LOCK_H_

typedef volatile int spinlock_t;

typedef struct rwlock {
	volatile int readers;
	struct thread *wrthread;
	spinlock_t reader, global;
} rwlock_t;

static inline void
lock(spinlock_t *lock)
{
	while (!__sync_bool_compare_and_swap(lock, 0, 1)) {
		while (*lock)
			__asm__("pause");
	}
}

static inline void
unlock(spinlock_t *lock)
{
	__sync_lock_release(lock);
}

static inline void
rwlock_rd(rwlock_t *rwl)
{
	lock(&rwl->reader);
	if (++rwl->readers == 1)
		lock(&rwl->global);
	unlock(&rwl->reader);
}

static inline void
rwlock_endrd(rwlock_t *rwl)
{
	lock(&rwl->reader);
	if(--rwl->readers == 0)
		unlock(&rwl->global);
	unlock(&rwl->reader);
}

static inline void rwlock_wr(rwlock_t *rwl)
{
	lock(&rwl->global);
}

static inline void rwlock_endwr(rwlock_t *rwl)
{
	unlock(&rwl->global);
}

#endif /* LOCK_H_ */
