#include <string.h>

void *memset(void *dst, int val, size_t size)
{
	unsigned char *p = dst;
	while(size--) {
		*p++ = val;
	}
	return dst;
}

