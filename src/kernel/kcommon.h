
#ifndef _KCOMMON_H
#define _KCOMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef ptrdiff_t ssize_t;

typedef struct {
	size_t count;
	const char *data;
} string_view;

//typedef _Bool bool;

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)


#define __must_check		__attribute__((__warn_unused_result__))
#ifdef __CHECKER__
#define __force	            __attribute__((force))
#else
#define __force
#endif

#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((typeof(x))(a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((typeof(x))(a) - 1))
#define INT_CEIL_DIV(x, y) (((x) + (y) - 1) / (y))

#define ARRAY_COUNT(arr) (sizeof((arr)) / sizeof((arr)[0]))

#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))

#define __CONTAINER_OF(ptr, type, member) ({				\
	void *__mptr = (void *)(ptr);					\
	static_assert(__same_type(*(ptr), ((type *)0)->member) ||	\
		      __same_type(*(ptr), void),			\
		      "pointer type mismatch in container_of()");	\
	((type *)(__mptr - offsetof(type, member))); })

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 */
#define container_of(ptr, type, member) 	\
	_Generic(ptr,							\
		const typeof(*(ptr)) *: ((const type *)__CONTAINER_OF(ptr, type, member)),\
		default: ((type *)__CONTAINER_OF(ptr, type, member))	\
	)

#define __STRINGIFY_(n) #n
#define __STRINGIFY(n) __STRINGIFY_(n)
#define ASSERT(expr) assert(expr)
#define assert(expr) do { if(!(expr)) { panic("Assertion failed in at" __FILE__ "(" __STRINGIFY(__LINE__) ")!"); } } while(false)
#define static_assert _Static_assert

__attribute__ ((noreturn)) 
void panic(const char *msg);

#define STR_LIT(cnst) (string_view){sizeof((cnst))-1, (cnst)}
#define str_compare_const(a, cnst) str_compare_const_((a), (cnst), sizeof((cnst))-1)

static inline string_view str_cstr(const char *cstr)
{
	return (string_view){strlen(cstr), cstr};
}

static inline string_view str_slice(const string_view a, size_t from, size_t count)
{
	return (string_view){count, a.data+from};
}

static inline bool str_compare(const string_view *a, const string_view *b)
{
	if(a->count != b->count)
		return false;
	return memcmp(a->data, b->data, b->count) == 0;
}

static inline bool str_compare_const_(const string_view *a, const char *cstr, size_t len)
{
	string_view b = {len, cstr};
	return str_compare(a, &b);
}

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

#define MAX_ERRNO	4095

#define IS_ERR_VALUE(x) unlikely((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)


static inline void * ERR_PTR(long error)
{
	return (void *) error;
}

static inline long __must_check PTR_ERR(__force const void *ptr)
{
	return (long) ptr;
}

static inline long __must_check PTR_ERR_OR(__force const void *ptr, long err)
{
	if (!ptr)
		return err;
	return (long) ptr;
}

static inline bool __must_check IS_ERR(__force const void *ptr)
{
	return IS_ERR_VALUE((unsigned long)ptr);
}

static inline int __must_check PTR_ERR_OR_ZERO(__force const void *ptr)
{
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);
	else
		return 0;
}

static inline bool __must_check IS_ERR_OR_NULL(__force const void *ptr)
{
	return unlikely(!ptr) || IS_ERR_VALUE((unsigned long)ptr);
}

int memcmp(const void *p1, const void *p2, size_t num);
void *memcpy(void *dst, const void *src, size_t num);
void *memset(void *dst, int val, size_t size);

#endif

