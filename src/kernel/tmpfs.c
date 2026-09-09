#include "tmpfs.h"

#include <vfs.h>
#include <kmalloc.h>
#include <kmem.h>
#include <errno.h>

void *tmpfs_add_page_to_inode(TmpFSINode *inode)
{
	void *page = kmem_map(PAGE_SIZE, PAGE_FLAG_RW);
	if (!page)
		return ERR_PTR(-ENOMEM);

	if (inode->page_table.page_count < TMPFS_DIRECT_PAGES)
	{
		inode->page_table.direct_pages[inode->page_table.page_count++] = page;
		return page;
	}

	size_t indirect_idx = (inode->page_table.page_count - TMPFS_DIRECT_PAGES) / TMPFS_PAGE_PER_INDIRECT;
	size_t indirect_page_idx = (inode->page_table.page_count - TMPFS_DIRECT_PAGES) % TMPFS_PAGE_PER_INDIRECT;
	if (indirect_page_idx == 0)
	{
		void **indirect = kmem_map(PAGE_SIZE, PAGE_FLAG_RW);
		if (!indirect) {
			kmem_unmap(page);
			return ERR_PTR(-ENOMEM);
		}
		inode->page_table.indirect_pages[indirect_idx] = indirect;

	}
	inode->page_table.indirect_pages[indirect_idx][indirect_page_idx] = page;
	inode->page_table.page_count++;
	
	return page;
}

void *tmpfs_get_or_make_page(INode *inode_, size_t page_idx)
{
	TmpFSINode *inode = container_of(inode_, TmpFSINode, base);

	if (page_idx >= inode->page_table.page_count) {
		void *page = NULL;
		while (page_idx >= inode->page_table.page_count) {
			page = tmpfs_add_page_to_inode(inode);
			if (IS_ERR_OR_NULL(page))
				return page;
		}
		return page;
	}


	if (page_idx < TMPFS_DIRECT_PAGES)
	{
		return inode->page_table.direct_pages[page_idx];
	}

	size_t indirect_idx = (page_idx - TMPFS_DIRECT_PAGES) / TMPFS_PAGE_PER_INDIRECT;
	size_t indirect_page_idx = (page_idx - TMPFS_DIRECT_PAGES) % TMPFS_PAGE_PER_INDIRECT;
	return inode->page_table.indirect_pages[indirect_idx][indirect_page_idx];
}

int tmpfs_open(INode *inode, File *file)
{
	file->fop = inode->fop;
	file->inode = inode;
	file->offset = 0;
	return 0;
}

void tmpfs_close(File *file)
{
	(void)file;
}

ssize_t tmpfs_write(File *file, void *buf, size_t size)
{
	size_t written = 0;
	while (written < size) {
		u8 *page = tmpfs_get_or_make_page(file->inode, file->offset/PAGE_SIZE);
		if (IS_ERR_OR_NULL(page)) {
			return written;
		}

		size_t page_offset = file->offset % PAGE_SIZE;
		size_t to_write = min(size-written, PAGE_SIZE-page_offset);
		memcpy(page+page_offset, buf+written, to_write);
		written += to_write;
		file->offset += to_write;
	}
	return written;
}

ssize_t tmpfs_read(File *file, void *buf, size_t size)
{
	size_t read = 0;
	while (read < size) {
		u8 *page = tmpfs_get_or_make_page(file->inode, file->offset/PAGE_SIZE);
		if (IS_ERR_OR_NULL(page)) {
			return read;
		}

		size_t page_offset = file->offset % PAGE_SIZE;
		size_t to_read = min(size-read, PAGE_SIZE-page_offset);
		memcpy(buf+read, page+page_offset, to_read);
		read += to_read;
		file->offset += to_read;
	}
	return read;
}

DirEntry *tmpfs_create(struct DirEntry *dir, const string_view *name, mode_t mode) {
	ASSERT(name);

	TmpFSINode *inode = container_of(dir->inode, TmpFSINode, base);
	INode *new = tmpfs_new_inode(inode, inode->base.sb, *name, mode);
	if (IS_ERR_OR_NULL(new))
		return ERR_PTR(PTR_ERR_OR(new, -ENOMEM));

	return make_dentry(*name, new, dir);
}

struct DirEntry *tmpfs_lookup(struct INode *inode_, DirEntry *parent, const string_view *name) {
	TmpFSINode *inode = container_of(inode_, TmpFSINode, base);
	TmpFSINode *it;
	list_for_each_entry(it, &inode->children, node) {
		if (str_compare(&it->name, name)) {
			return make_dentry(*name, &it->base, parent);
		}
	}
	return ERR_PTR(-ENOENT);
}

i64 tmpfs_get_size(INode *inode_)
{
	TmpFSINode *inode = container_of(inode_, TmpFSINode, base);
	return (i64)inode->page_table.page_count * PAGE_SIZE;
}

ssize_t tmpfs_readdir(File *file, void *buf, size_t size)
{
	if (size > SSIZE_MAX)
		return -EINVAL;

	TmpFSINode *inode = container_of(file->inode, TmpFSINode, base);
	TmpFSINode *it;
	DirInfo *arr = buf;
	ssize_t read = 0;
	list_for_each_entry(it, &inode->children, node) {
		if (read == (ssize_t)size)
			break;

		arr[read].name_len = it->name.count;
		arr[read].name = kzalloc(it->name.count);
		if (!arr[read].name) {
			vfs_free_readdir_entries(arr, read);
			return -ENOMEM;
		}
		memcpy(arr[read].name, it->name.data, it->name.count);
		read++;
	}
	return read;
}

static FileOps tmpfs_fops = {
	.open = fop_generic_open,
	.close = tmpfs_close,
	.write = tmpfs_write,
	.read = tmpfs_read,
	.readdir = tmpfs_readdir,
	.seek = fop_generic_seek,
};

static INodeOps tmpfs_ops = {
	.create = tmpfs_create,
	.lookup = tmpfs_lookup,
	.get_size = tmpfs_get_size,
};

INode *tmpfs_new_inode(TmpFSINode *parent, SuperBlock *block, string_view name, mode_t mode)
{
	TmpFSINode *inode = kzalloc(sizeof(TmpFSINode));
	if (!inode) {
		return ERR_PTR(-ENOMEM);
	}
	inode->name = name;
	TmpFSSBInfo *info = block->private;

	FileOps fops = tmpfs_fops;
	if (S_ISCHR(mode) || S_ISBLK(mode))
		fops = vfs_stub_fileops;

	vfs_fill_inode(&inode->base, info->inode_idx, tmpfs_ops, fops, mode, block);
	info->inode_idx++;
	
	list_init(&inode->node);
	list_init(&inode->children);

	if (parent) {
		list_add(&parent->children, &inode->node);
	}
	return &inode->base;
}

int tmpfs_fill_super(SuperBlock *block)
{
	TmpFSSBInfo *info = kzalloc(sizeof(TmpFSSBInfo));
	if (!info)
		return -ENOMEM;

	info->inode_idx = 1;

	block->private = info;

	INode *root = tmpfs_new_inode(NULL, block, STR_LIT("/"), 0777);
	if (!root) {
		kfree(info);
		return -ENOMEM;
	}
	block->root = make_dentry(STR_LIT(""), root, NULL);
	if (IS_ERR_OR_NULL(block->root)) {
		kfree(info);
		return PTR_ERR(block->root);
	}

	return 0;
}

DirEntry *tmpfs_mount(FileSystem *fs, string_view dev, int flags)
{
	(void)dev; // don't need device
	return mount_nodev(fs, flags, tmpfs_fill_super);
}

static FileSystem tmpfs = {
	.name = STR_LIT("tmpfs"),
	.mount = tmpfs_mount,
	.requires_bdev = false,
};

int tmpfs_init()
{
	return fs_register(&tmpfs);
}

