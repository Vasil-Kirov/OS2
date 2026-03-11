
#ifndef _MSR_H
#define _MSR_H

#include <kcommon.h>
#include <kcpuid.h>

static const u32 CPUID_FLAG_MSR = 1 << 5;

static inline bool cpu_has_msr()
{
   static u32 a, d; // eax, edx
   u32 unused;
   __get_cpuid(1, &a, &unused, &unused, &d);
   return d & CPUID_FLAG_MSR;
}

static inline void cpu_get_msr(uint32_t msr, uint32_t *lo, uint32_t *hi)
{
   __asm__ volatile("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(msr));
}

static inline void cpu_set_msr(uint32_t msr, uint32_t lo, uint32_t hi)
{
   __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}



#endif // _MSR_H

