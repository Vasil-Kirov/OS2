#include "block.h"
#include "kmem.h"
#include <errno.h>


int block_read(BlockDevice *blk, u64 offset, size_t size, void *buf)
{
	if(size == 0)
		return 0;

	u64 read_offset = offset%blk->block_size;

	u64 start_idx = offset/blk->block_size;
	u64 end_idx = (offset+size-1)/blk->block_size;

	u64 block_count = end_idx-start_idx+1;

	void *vaddr;
	uintptr_t paddr;
	if (!dma_map(block_count * blk->block_size, &vaddr, &paddr)) {
		return -ENOMEM;
	}

	int ret = blk->read(blk, start_idx, end_idx-start_idx+1, paddr);
	if (ret != 0) {
		dma_unmap(paddr);
		return ret;
	}
	
	memcpy(buf, (u8 *)vaddr+read_offset, size);
	dma_unmap(paddr);
	return 0;
}

int block_nvme_write(BlockDevice *blk, u64 block_idx, u16 block_count, uintptr_t buf)
{
	NVMeDevice *dev = blk->data;
	if (!nvme_write(dev, 1, block_idx, block_count, buf))
		return -EIO;
	return 0;
}

int block_nvme_read(BlockDevice *blk, u64 block_idx, u16 block_count, uintptr_t buf)
{
	NVMeDevice *dev = blk->data;
	if (!nvme_read(dev, 1, block_idx, block_count, buf))
		return -EIO;
	return 0;
}

int block_from_nvme(BlockDevice *block, NVMeDevice *dev)
{
	if (dev->ns_count == 0)
		return -ENODEV;

	block->write = block_nvme_write;
	block->read = block_nvme_read;
	block->block_count = dev->ns_infos[0].num_blocks;
	block->block_size = dev->ns_infos[0].block_size;
	block->data = dev;
	return 0;
}

