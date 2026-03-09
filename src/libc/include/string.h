
#ifndef STRING_H
#define STRING_H

#include <stddef.h>

size_t strlen(const char *s);
int memcmp(const void *p1, const void *p2, size_t num);
void *memcpy(void *dst, const void *src, size_t num);

#endif

