#include "block.h"
#include <kmem.h>
#include <kmalloc.h>
#include <vfs.h>
#include <errno.h>

ssize_t bdev_read(struct File *file, void *buf, size_t size)
{
	if (size == 0)
		return 0;

	BlockDevice *bdev = file->inode->priv;
	if (bdev->block_size == 0 || bdev->block_count == 0)
		return -EINVAL;

	u64 offset = file->offset;

	u64 read_offset = offset%bdev->block_size;

	u64 start_idx = offset/bdev->block_size;
	u64 end_idx = (offset+size-1)/bdev->block_size;
	if (end_idx >= bdev->block_count)
		end_idx = bdev->block_count;
	if (end_idx < start_idx)
		return -EINVAL;

	u64 block_count = end_idx-start_idx+1;

	void *vaddr;
	uintptr_t paddr;
	if (!dma_map(block_count * bdev->block_size, &vaddr, &paddr)) {
		return -ENOMEM;
	}

	int ret = bdev->read(bdev, start_idx, block_count, paddr);
	if (ret != 0) {
		dma_unmap(vaddr);
		return ret;
	}
	
	memcpy(buf, (u8 *)vaddr+read_offset, size);
	dma_unmap(vaddr);
	file->offset += size;
	return size;
}

u64 bdev_seek(File *file, u64 offset, SeekFrom from)
{
	if (from == SeekFrom_Start)
		file->offset = offset;
	if (from == SeekFrom_Curr)
		file->offset += offset;
	return file->offset;
}

FileOps bdev_fops = {
	.open = fop_generic_open,
	.read = bdev_read,
	.write = NULL, // @TODO:
	.seek = bdev_seek,
};

int bdev_create(Device *dev, BDevWriteFn write, BDevReadFn read, size_t block_count, size_t block_size, void *data)
{
	BlockDevice *bdev = kzalloc(sizeof(BlockDevice));
	if (IS_ERR_OR_NULL(bdev))
		return PTR_ERR_OR(bdev, -ENOMEM);

	bdev->write = write;
	bdev->read = read;
	bdev->block_count = block_count;
	bdev->block_size = block_size;
	bdev->data = data;

	DirEntry *devdir = vfs_find(STR_LIT("/dev"));
	if (IS_ERR_OR_NULL(devdir)) {
		kfree(bdev);
		return PTR_ERR_OR(devdir, -ENOENT);
	}

	DirEntry *deve = devdir->inode->op.create(devdir, &dev->name, S_IFBLK | 0777);
	if (IS_ERR_OR_NULL(deve)) {
		kfree(bdev);
		return PTR_ERR_OR(deve, -EIO);
	}

	deve->inode->fop = bdev_fops;
	deve->inode->priv = bdev;
	return 0;
}

