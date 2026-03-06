
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


#define ARRAY_COUNT(arr) (sizeof((arr)) / sizeof((arr)[0]))

__attribute__((noreturn))
void panic(const char *err);

#endif

