
#ifndef _BSTREAM_H
#define _BSTREAM_H
#include <kcommon.h>


typedef struct {
	u8 *start;
	u8 *at;
	u8 *end;
	int err;
} BinaryStream;

static inline size_t bs_remaining_size(BinaryStream *bs)
{
	return bs->end - bs->at;
}

static inline bool bs_validate_read_size(BinaryStream *bs, size_t size)
{
	if (!bs->at)
		return false;
	if (bs->end < bs->at)
		return false;
	return (size_t)(bs->end - bs->at) >= size;
}

static inline void bs_init(BinaryStream *bs, u8 *ptr, size_t size)
{
	bs->start = ptr;
	bs->at = ptr;
	bs->end = ptr+size;
	bs->err = 0;
}

static inline u8 bs_r8(BinaryStream *bs)
{
	if (!bs_validate_read_size(bs, 1))
	{
		bs->err = -EIO;
		return 0;
	}
	u8 b = *bs->at;
	bs->at++;
	return b;
}

static inline u8 bs_r16(BinaryStream *bs)
{
	if (!bs_validate_read_size(bs, 2))
	{
		bs->err = -EIO;
		return 0;
	}
	u16 b = *(u16 *)bs->at;
	bs->at += 2;
	return b;
}

static inline u32 bs_r32(BinaryStream *bs)
{
	if (!bs_validate_read_size(bs, 4))
	{
		bs->err = -EIO;
		return 0;
	}
	u32 b = *(u32 *)bs->at;
	bs->at += 4;
	return b;
}

static inline void *bs_rsz(BinaryStream *bs, size_t size)
{
	if (!bs_validate_read_size(bs, size))
	{
		bs->err = -EIO;
		return NULL;
	}
	void *p = bs->at;
	bs->at += size;
	return p;
}

static inline bool bs_seek(BinaryStream *bs, size_t absolute_offset)
{
	if (bs->start + absolute_offset >= bs->end)
		return false;
	bs->at = bs->start + absolute_offset;
	return true;
}


#endif

