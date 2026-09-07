#pragma once
#include <sync_prim.h>

typedef struct {
	RawLock arch_lock;
} Spinlock;

#define SPINLOCK_INIT (Spinlock){RAW_LOCK_INIT}


static inline void spin_lock(Spinlock *lock)
{
	rawsl_lock(&lock->arch_lock);
}

static inline void spin_unlock(Spinlock *lock)
{
	rawsl_unlock(&lock->arch_lock);
}

