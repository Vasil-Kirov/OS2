
#ifndef _BLOCK_H
#define _BLOCK_H

#include <kcommon.h>
#include <drivers.h>

struct BlockDevice;
typedef int (*BDevWriteFn)(struct BlockDevice *blk, u64 block_idx, u16 block_count, uintptr_t buf);
typedef int (*BDevReadFn)(struct BlockDevice *blk, u64 block_idx, u16 block_count, uintptr_t buf);

typedef struct BlockDevice {
	BDevWriteFn write;
	BDevReadFn read;
	size_t block_count;
	size_t block_size;
	void *data;
} BlockDevice;


int bdev_create(Device *dev, BDevWriteFn write, BDevReadFn read, size_t block_count, size_t block_size, void *data);
//int block_read(BlockDevice *blk, u64 offset, size_t size, void *buf);
//int block_from_nvme(BlockDevice *block, NVMeDevice *dev);

#endif // _BLOCK_H

