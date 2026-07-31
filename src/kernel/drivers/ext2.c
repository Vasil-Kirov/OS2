#include "kprintf.h"
#include <drivers/ext2.h>
#include <drivers/vfs.h>
#include <block.h>
#include <kmem.h>
#include <errno.h>

int ext2_read_blocks(Ext2FS *fs, u32 block, u32 block_count, void *buf)
{
	return block_read(fs->blk, block * fs->block_size , block_count * fs->block_size, buf);
}

Ext2FindState *ext2_find_first_file(Ext2FS *fs, Ext2INode *dir)
{
	if ((dir->mode & Ext2INode_Dir) == 0)
		return NULL;

	Ext2FindState *find = kmem_map(sizeof(Ext2FindState), PAGE_FLAG_RW);
	if (!find)
		return NULL;

	find->buf = kmem_map(fs->block_size, PAGE_FLAG_RW);
	if (!find->buf) {
		kmem_unmap(find);
		return NULL;
	}

	int ret = ext2_read_blocks(fs, dir->block[0], 1, find->buf);
	if (ret != 0) {
		kmem_unmap(find->buf);
		kmem_unmap(find);
		return NULL;
	}

	find->dir = dir;
	find->entry = (Ext2DirEntry *)&find->buf[0];
	return find;
}

int ext2_find_next_file(Ext2FS *fs, Ext2FindState *find)
{
	find->offset += find->entry->size;
	find->entry = (Ext2DirEntry *)&find->buf[find->offset];
	// @TODO: read other blocks
	return 0;
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

INode *ext2_lookup(INode *parent, string_view *name)
{
	Ext2FS *fs = parent->private;
	Ext2INode inode;
	int ret = ext2_read_inode(fs, parent->inum, &inode);
	if (ret != 0)
		return NULL;

	Ext2FindState *find = ext2_find_first_file(fs, &inode);
	if (!find)
		return NULL;

	string_view entryn = ext2_dentry_name(fs, find->entry);
	if (str_compare(name, &entryn)) {
		// @TODO:
	} else {
		while (true) {
		}
	}



	INode *r = kmem_map(sizeof(INode), PAGE_FLAG_RW);
}

static DirOps ext2_dir_ops = {
	ext2_lookup,
};

static FileOps ext2_file_ops = {
	NULL, // TODO:
	NULL, // TODO:
};

int ext2_create_inode(Ext2FS *fs, u32 inum, INode *i)
{
	Ext2INode inode;
	int ret = ext2_read_inode(fs, inum, &inode);
	if (ret != 0)
		return ret;


	return 0;
}

void ext2_init_vfs(VFS *fs)
{
}

/*
 * Reference
int ext2_read_root(Ext2FS *fs)
{
	int ret = 0;
	Ext2Inode root = {};
	ret = ext2_read_inode(fs, 2, &root);
	if (ret != 0)
		return ret;

	if ((root.mode & Ext2INode_Dir) == 0)
		return -EINVAL;

	u8 buf[8192];
	assert(fs->block_size <= 8192);
	ret = ext2_read_blocks(fs, root.block[0], 1, buf);
	if (ret != 0)
		return ret;

	size_t offset = 0;
	while (offset < fs->block_size) {
		Ext2DirEntry *entry = (Ext2DirEntry *)&buf[offset];

		if (entry->size == 0)
			break;
		offset += entry->size;
	}


	return ret;
}
*/

int ext2_read_inode(Ext2FS *fs, u32 inode_num, Ext2INode *inode)
{
	if (inode_num == 0)
		return -EINVAL;
	u32 index = inode_num - 1;

	u32 group = index / fs->sb.inodes_per_group;
	u32 offset = index % fs->sb.inodes_per_group;
	if (group > fs->group_count)
		return -EINVAL;

	u32 inode_table = fs->groups[group].inode_table;
	u32 read_offset = inode_table * fs->block_size + offset * fs->inode_size;

	return block_read(fs->blk, read_offset, fs->inode_size, inode);
}

int ext2_init(Ext2FS *fs, BlockDevice *blk)
{
	int ret = 0;
	fs->blk = blk;
	ret = block_read(fs->blk, 1024, sizeof(Ext2Superblock), &fs->sb);
	if (ret != 0)
		return ret;

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
	ret = block_read(fs->blk, bgdt_block * fs->block_size , fs->group_count * sizeof(Ext2GroupDescriptor), fs->groups);
	if (ret != 0) {
		kmem_unmap(fs->groups);
		return ret;
	}


	return ret;
}

