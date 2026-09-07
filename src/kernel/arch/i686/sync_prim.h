
#ifndef _SYNC_PRIM_H
#define _SYNC_PRIM_H

#include <kcommon.h>

#define RAW_LOCK_INIT (0)

typedef u32 RawLock;

static inline unsigned int atomic_uincrement(volatile unsigned int *p)
{
    unsigned int old = 1;
    __asm__ __volatile__ (
        "lock xadd %1, %0"
        : "+r" (old), "+m" (*p)
        :
        : "memory", "cc"
    );
    return old + 1;
}

static inline int atomic_increment(volatile int *p)
{
    int old = 1;
    __asm__ __volatile__ (
        "lock xadd %1, %0"
        : "+r" (old), "+m" (*p)
        :
        : "memory", "cc"
    );
    return old + 1;
}

static inline void rawsl_lock(RawLock *l)
{
	__asm__ volatile (
			"1:\n"
			"lock bts DWORD PTR [%0], 0\n"
			"jnc 2f\n"
			"pause\n"
			"jmp 1b\n"
			"2:"
			:
			:"r"(l)
			: "cc", "memory"
			);
}

static inline void rawsl_unlock(RawLock *l)
{
	__asm__ volatile (
			"lock btr DWORD PTR [%0], 0\n"
			:
			:"r"(l)
			: "cc", "memory"
			);
}


#endif // _SYNC_PRIM_H

