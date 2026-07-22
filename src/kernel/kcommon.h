
#ifndef _KCOMMON_H
#define _KCOMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

//typedef _Bool bool;

#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((typeof(x))(a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((typeof(x))(a) - 1))

#define ARRAY_COUNT(arr) (sizeof((arr)) / sizeof((arr)[0]))

#define __STRINGIFY_(n) #n
#define __STRINGIFY(n) __STRINGIFY_(n)
#define assert(expr) do { if(!(expr)) { panic("Assertion failed in at" __FILE__ "(" __STRINGIFY(__LINE__) ")!"); } } while(false)
#define static_assert _Static_assert

__attribute__ ((noreturn)) 
void panic(const char *msg);

static inline u8 read8(void volatile *mem)
{
	return *(u8 volatile *)mem;
}

static inline u16 read16(void volatile *mem)
{
	return *(u16 volatile *)mem;
}

static inline u32 read32(void volatile *mem)
{
	return *(u32 volatile *)mem;
}

static inline void write32(void volatile *mem, u32 dword)
{
	*(u32 volatile *)mem = dword;
}

static inline u64 read64(void volatile *mem)
{
	return *(u64 volatile *)mem;
}

static inline void write64(void volatile *mem, u64 qword)
{
	*(u64 volatile *)mem = qword;
}

int memcmp(const void *p1, const void *p2, size_t num);
void *memcpy(void *dst, const void *src, size_t num);
void *memset(void *dst, int val, size_t size);

#endif

