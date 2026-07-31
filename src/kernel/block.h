
#ifndef _BLOCK_H
#define _BLOCK_H

#include <kcommon.h>
#include "drivers/nvme.h"

typedef struct BlockDevice {
	int (*write)(struct BlockDevice *blk, u64 block_idx, u16 block_count, uintptr_t buf);
	int (*read)(struct BlockDevice *blk, u64 block_idx, u16 block_count, uintptr_t buf);
	size_t block_count;
	size_t block_size;
	void *data;
} BlockDevice;


int block_read(BlockDevice *blk, u64 offset, size_t size, void *buf);
int block_from_nvme(BlockDevice *block, NVMeDevice *dev);

#endif // _BLOCK_H

