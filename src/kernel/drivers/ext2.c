#include "kmalloc.h"
#include "kprintf.h"
#include <drivers/ext2.h>
#include <vfs.h>
#include <block.h>
#include <kmem.h>
#include <errno.h>

#define EXT2_ROOT_INO (2)

ssize_t ext2_read_blocks_offset(Ext2FS *fs, u32 block, u32 block_count, void *buf, size_t buf_limit, u64 offset)
{
	File *dev = fs->fsb->dev;
	dev->fop.seek(dev, ((u64)block * (u64)fs->block_size)+offset, SeekFrom_Start);
	return dev->fop.read(dev, buf, min((u64)buf_limit, (u64)block_count * (u64)fs->block_size));
}

ssize_t ext2_read_blocks_with_limit(Ext2FS *fs, u32 block, u32 block_count, void *buf, size_t buf_limit)
{
	File *dev = fs->fsb->dev;
	dev->fop.seek(dev, (u64)block * (u64)fs->block_size, SeekFrom_Start);
	return dev->fop.read(dev, buf, min((u64)buf_limit, (u64)block_count * (u64)fs->block_size));
}

ssize_t ext2_read_blocks(Ext2FS *fs, u32 block, u32 block_count, void *buf)
{
	File *dev = fs->fsb->dev;
	dev->fop.seek(dev, block * fs->block_size, SeekFrom_Start);
	return dev->fop.read(dev, buf, block_count * fs->block_size);
}

static int ext2_bmap(Ext2FS *fs, Ext2INode *inode, u32 logical_block, u32 *out)
{
	u64 entries_per_block = fs->block_size / sizeof(u32);
	u32 idx[3];
	int depth;

	if (logical_block < 12) {
		*out = inode->base.block[logical_block];
		return 0;
	}
	logical_block -= 12;

	if (logical_block < entries_per_block) {
		depth = 1;
		idx[0] = logical_block;
	} else if (logical_block - entries_per_block < entries_per_block * entries_per_block) {
		logical_block -= entries_per_block;
		depth = 2;
		idx[0] = logical_block / entries_per_block;
		idx[1] = logical_block % entries_per_block;
	} else {
		logical_block -= entries_per_block + entries_per_block * entries_per_block;
		depth = 3;
		idx[0] = logical_block / (entries_per_block * entries_per_block);
		idx[1] = (logical_block / entries_per_block) % entries_per_block;
		idx[2] = logical_block % entries_per_block;
	}

	u32 block = inode->base.block[12 + depth - 1];
	for (int i = 0; i < depth; ++i) {
		u32 next;
		if (block == 0) {           /* hole */
			*out = 0;
			return 0;
		}
		ssize_t read = ext2_read_blocks_offset(fs, block, 1, &next, sizeof(next), idx[i] * sizeof(u32));
		if (read < 0)
			return -EIO;
		block = next;
	}
	*out = block;
	return 0;
}

u64 ext2_inode_size(Ext2INode *inode)
{
	u64 size = inode->base.size_low;
	Ext2FS *fs = inode->inode.sb->private;
	if (fs->sb.ver_major >= 1 && !S_ISDIR(inode->base.mode))
	{
		size |= ((u64)inode->base.size_high) << 32;
	}
	return size;
}

void ext2_find_close(Ext2FindState *find)
{
	kmem_unmap(find->buf);
	kmem_unmap(find);
}

string_view ext2_dentry_name(Ext2FS *fs, Ext2DirEntry *entry)
{
	u32 len = entry->name_len_lo;
	if((fs->sb.required_features & EXT2_FEATURE_INCOMPAT_FILETYPE) == 0)
		len |= (u32)entry->file_type_or_name_len_hi << 8;

	return (string_view) {.count = len, .data = (const char *)&entry->name[0]};
}

/* 0 = entry loaded, 1 = end of directory, <0 = error */
static int ext2_find_load(Ext2FS *fs, Ext2FindState *find)
{
	const size_t DIR_ENTRY_SIZE = offsetof(Ext2DirEntry, name);
	for (;;) {
		if (find->offset + DIR_ENTRY_SIZE > find->size)
			return 1;

		u32 logical_block = find->offset / fs->block_size;
		size_t block_offset = find->offset % fs->block_size;

		if (logical_block != find->cached_block) {
			u32 pblk;
			if (ext2_bmap(fs, find->dir, logical_block, &pblk) < 0)
				return -EIO;
			if (pblk == 0) {   /* hole */
				find->offset = (u64)(logical_block + 1) * fs->block_size;
				continue;
			}
			if (ext2_read_blocks(fs, pblk, 1, find->buf) < 0)
				return -EIO;
			find->cached_block = logical_block;
		}

		Ext2DirEntry *e = (Ext2DirEntry *)&find->buf[block_offset];
		u16 rec = e->size;
		if (rec < DIR_ENTRY_SIZE || (rec & 3) != 0 || block_offset + rec > fs->block_size)
			return -EIO;
		if (DIR_ENTRY_SIZE + ext2_dentry_name(fs, e).count > rec)
			return -EIO;

		if (e->inode == 0) {       /* deleted slot */
			find->offset += rec;
			continue;
		}
		find->entry = e;
		return 0;
	}
}

Ext2FindState *ext2_find_first_file(Ext2FS *fs, Ext2INode *dir)
{
	if (!S_ISDIR(dir->base.mode))
		return NULL;

	Ext2FindState *find = kmem_map(sizeof(Ext2FindState), PAGE_FLAG_RW);
	if (!find)
		return NULL;

	find->buf = kmem_map(fs->block_size, PAGE_FLAG_RW);
	if (!find->buf) {
		kmem_unmap(find);
		return NULL;
	}

	find->dir    = dir;
	find->size   = ext2_inode_size(dir);
	find->offset = 0;
	find->cached_block = UINT32_MAX;
	if (ext2_find_load(fs, find) != 0) {
		ext2_find_close(find);
		return NULL;
	}

	return find;
}

int ext2_find_next_file(Ext2FS *fs, Ext2FindState *find)
{
	find->offset += find->entry->size;
	return ext2_find_load(fs, find);
}

DirEntry *ext2_lookup(INode *inode_, DirEntry *parent, const string_view *name)
{
	Ext2FS *fs = inode_->sb->private;
	Ext2INode *inode = container_of(inode_, Ext2INode, inode);
	Ext2FindState *find = ext2_find_first_file(fs, inode);
	if (!find)
		return NULL;

	DirEntry *r = NULL;
	do {
		string_view entryn = ext2_dentry_name(fs, find->entry);
		if (str_compare(name, &entryn)) {
			INode *child = ext2_new_inode(fs, find->entry->inode);
			if (IS_ERR_OR_NULL(child)) {
				r = (void *)child;
				break;
			}

			r = make_dentry(*name, child, parent);
			break;
		}
	} while(ext2_find_next_file(fs, find) == 0);

	ext2_find_close(find);
	return r;
}

ssize_t ext2_write(File *file, void *buf, size_t size)
{
	(void)file;
	(void)buf;
	(void)size;
	return 0;
};

ssize_t ext2_read(File *file, void *buf, size_t size)
{
	Ext2INode *inode = container_of(file->inode, Ext2INode, inode);
	Ext2FS *fs = inode->inode.sb->private;
	size_t original_size = size;

	u8 *p = buf;
	u64 off = file->offset;
	u64 fsize = ext2_inode_size(inode);

	if (off >= fsize)
		return 0;
	if (size > fsize - off)
		size = fsize - off;

	while (size) {
		u32 logical_block = off / fs->block_size;
		size_t block_offset = off % fs->block_size;
		size_t n = min(size, fs->block_size - block_offset);
		u32 file_block;

		if (ext2_bmap(fs, inode, logical_block, &file_block) < 0)
			break;
		if (file_block == 0)
			memset(p, 0, n);
		else if (ext2_read_blocks_offset(fs, file_block, 1, p, n, block_offset) < 0)
			break;

		p += n;
		off += n;
		size -= n;
	}

	file->offset = off;
	if (p == buf && original_size > 0)
		return -EIO;

	return p - (u8 *)buf;
}

u64 ext2_seek(File *file, u64 offset, SeekFrom from)
{
	if (from == SeekFrom_Start)
		file->offset = offset;
	if (from == SeekFrom_Curr)
		file->offset += offset;
	return file->offset;
}

void ext2_close(File *file)
{
	(void)file;
}

i64 ext2_get_size(struct INode *inode)
{
	return ext2_inode_size(container_of(inode, Ext2INode, inode));
}

static INodeOps ext2_inode_ops = {
	.create = NULL, // @TODO:
	.lookup = ext2_lookup,
	.get_size = ext2_get_size,
};

static FileOps ext2_file_ops = {
	.open = fop_generic_open,
	.close = ext2_close,
	.write = NULL,
	.read = ext2_read,
	.seek = ext2_seek,
};

ssize_t ext2_read_inode(Ext2FS *fs, u32 inode_num, Ext2INodeBase *inode)
{
	if (inode_num == 0)
		return -EINVAL;
	u64 index = inode_num - 1;

	u64 group = index / fs->sb.inodes_per_group;
	u64 offset = index % fs->sb.inodes_per_group;
	if (group > fs->group_count)
		return -EINVAL;

	u64 inode_table = fs->groups[group].inode_table;
	u64 read_offset = inode_table * fs->block_size + offset * fs->inode_size;

	File *dev = fs->fsb->dev;
	dev->fop.seek(dev, read_offset, SeekFrom_Start);
	return dev->fop.read(dev, inode, fs->inode_size);
}

INode *ext2_new_inode(Ext2FS *fs, u32 inode_num)
{
	Ext2INode *inode = kzalloc(sizeof(Ext2INode));
	if (!inode) {
		return ERR_PTR(-ENOMEM);
	}
	if (ext2_read_inode(fs, inode_num, &inode->base) != sizeof(Ext2INodeBase)) {
		kfree(inode);
		return ERR_PTR(-EIO);
	}
	FileOps fops = ext2_file_ops;
	if (S_ISCHR(inode->base.mode) || S_ISBLK(inode->base.mode))
		fops = vfs_stub_fileops;

	vfs_fill_inode(&inode->inode, inode_num, ext2_inode_ops, fops, inode->base.mode, fs->fsb);
	return &inode->inode;
}

void ext2_free(Ext2FS *fs)
{
	// @TODO:
	(void)fs;
}

int ext2_fill_super(SuperBlock *sb)
{
	if (IS_ERR_OR_NULL(sb->dev))
		return -EINVAL;

	Ext2FS *fs = kzalloc(sizeof(Ext2FS));
	if (!fs)
		return -ENOMEM;

	sb->private = fs;

	fs->fsb = sb;
	File *dev = fs->fsb->dev;

	int ret = 0;

	dev->fop.seek(dev, 1024, SeekFrom_Start);
	ssize_t read = dev->fop.read(dev, &fs->sb, sizeof(Ext2Superblock));
	if (read < 0)
		return (int)read;

	if (fs->sb.ext2_sig != EXT2_SIG)
		return -EINVAL;
	
	if (fs->sb.required_features & EXT2_FEATURE_INCOMPAT_COMPRESSION)
		return -ENOTSUP;
	if(fs->sb.required_features & EXT2_FEATURE_INCOMPAT_RECOVER)
		return -ENOTSUP;
	if(fs->sb.required_features & EXT2_FEATURE_INCOMPAT_JOURNAL_DEV)
		return -ENOTSUP;

	fs->block_size = 1024 << fs->sb.block_size_log2;
	if (fs->sb.ver_major < 1)
		fs->inode_size = 128;
	else
		fs->inode_size = fs->sb.inode_struct_size;

	fs->group_count = INT_CEIL_DIV(fs->sb.block_count, fs->sb.blocks_per_group);
	if (fs->group_count != INT_CEIL_DIV(fs->sb.inode_count, fs->sb.inodes_per_group)) {
		return -EINVAL;
	}

	kprintf("EXT2: Superblock");
	kprintf("\tblock_size:  %d", fs->block_size);
	kprintf("\tinode_size:  %d", fs->inode_size);
	kprintf("\tgroup_count: %d", fs->group_count);

	fs->groups = kmem_map(fs->group_count * sizeof(Ext2GroupDescriptor), PAGE_FLAG_RW);
	if (!fs->groups) {
		return -ENOMEM;
	}

	u32 bgdt_block = (fs->block_size == 1024) ? 2 : 1;
	dev->fop.seek(dev, bgdt_block * fs->block_size, SeekFrom_Start);
	read = dev->fop.read(dev, fs->groups, fs->group_count * sizeof(Ext2GroupDescriptor));
	if (read < 0) {
		kmem_unmap(fs->groups);
		return (int)read;
	}

	INode *root = ext2_new_inode(fs, EXT2_ROOT_INO);
	if (IS_ERR_OR_NULL(root)) {
		ext2_free(fs);
		return -EIO;
	}
	sb->root = make_dentry(STR_LIT(""), root, NULL);
	if (IS_ERR_OR_NULL(sb->root)) {
		ext2_free(fs);
		if (sb->root == NULL)
			return -ENOMEM;
		return PTR_ERR(sb->root);
	}

	return ret;
}

DirEntry *ext2_mount(FileSystem *fs, string_view dev_path, int flags)
{
	return mount_bdev(fs, dev_path, flags, ext2_fill_super);
}

static FileSystem ext2fs = {
	.name = STR_LIT("ext2"),
	.mount = ext2_mount,
	.requires_bdev = true,
};

int ext2_init()
{
	return fs_register(&ext2fs);
}

