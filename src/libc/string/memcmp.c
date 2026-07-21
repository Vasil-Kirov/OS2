
#include <stddef.h>
#include <stdint.h>

int memcmp(const void *p1, const void *p2, size_t num)
{
	const uint8_t *p1u8 = p1;
	const uint8_t *p2u8 = p2;
	for(size_t i = 0; i < num ; ++i) {
		if(p1u8[i] != p2u8[i])
			return p1u8[i] - p2u8[i];
	}
	return 0;
}


