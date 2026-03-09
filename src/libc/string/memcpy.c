#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t num)
{
	uint8_t *dstu8 = dst;
	const uint8_t *srcu8 = src;
	for(size_t i = 0; i < num; ++i)
		dstu8[i] = srcu8[i];
	return dst;
}

