
#ifndef _KCOMMON_H
#define _KCOMMON_H

#include <stdint.h>
#include <stddef.h>

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

__attribute__ ((noreturn)) 
void panic(const char *msg);

static inline u8 read8(void *mem)
{
	return *(u8 *)mem;
}

static inline u16 read16(void *mem)
{
	return *(u16 *)mem;
}

static inline u32 read32(void *mem)
{
	return *(u32 *)mem;
}

static inline void write32(void *mem, u32 dword)
{
	*(u32 *)mem = dword;
}

#endif

