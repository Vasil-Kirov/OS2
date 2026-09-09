#include "vfs.h"
#include <kcommon.h>
#include <kmalloc.h>
#include <errno.h>

VFS vfs;

int vfs_setup_dev()
{
	string_view dev_dir = STR_LIT("dev");
	DirEntry *dev = vfs.root->inode->op.create(vfs.root, &dev_dir, 0777);
	if (IS_ERR_OR_NULL(dev)) {
		return PTR_ERR_OR(dev, -ENOMEM);
	}
	int r = vfs_mount(STR_LIT("tmpfs"), STR_LIT("/dev"), STR_LIT(""), 0777);
	if (r != 0) {
		return r;
	}

	return 0;
}

string_view vfs_get_search_from_path(const string_view path)
{
	if (path.count == 0 || path.data[0] != '/')
		return path;

	for (size_t i = 1; i < path.count; ++i) {
		if (path.data[i] == '/') {
			return str_slice(path, 1, i-1);
		}
	}

	return str_slice(path, 1, path.count-1);
}

string_view vfs_search_next(const string_view path)
{
	if (path.count == 0 || path.data[0] != '/')
		return STR_LIT("");

	for (size_t i = 1; i < path.count; ++i) {
		if (path.data[i] == '/') {
			return str_slice(path, i, path.count-i);
		}
	}
	return STR_LIT("");
}

DirEntry *vfs_find(string_view path)
{
	if (!vfs.root)
		return ERR_PTR(-ENODEV);

	if (path.count == 0 || path.data[0] != '/')
		return ERR_PTR(-EINVAL);

	DirEntry *at = vfs.root;
	while (path.count != 0 && !IS_ERR_OR_NULL(at)) {
		// @TODO: . and ..
		// for .. look for mount points and resolve to mount_on->.. 
		// Vasko - 01/08/2026
		if (at->mount)
			at = at->mount->root;

		const string_view name = vfs_get_search_from_path(path);
		path = vfs_search_next(path);
		if (name.count == 0)
			continue;

		bool found = false;
		DirEntry *it;
		list_for_each_entry(it, &at->children, node) {
			if (str_compare(&name, &it->name)) {
				found = true;
				at = it;
				break;
			}
		}

		if (!found) {
			if (!at->inode) {
				return ERR_PTR(-ENOENT);
			}
			if (!S_ISDIR(at->inode->mode) || !at->inode->op.lookup) {
				return ERR_PTR(-ENOENT);
			}
			DirEntry *next = at->inode->op.lookup(at->inode, at, &name);
			if (IS_ERR_OR_NULL(next)) {
				return ERR_PTR(PTR_ERR_OR(next, -ENOENT));
			}
			at = next;
		}
	}

	if (!IS_ERR_OR_NULL(at) && at->mount)
		at = at->mount->root;
	return at;
}

File *vfs_open(string_view path)
{
	File *f = kzalloc(sizeof(File));
	if (!f)
		return ERR_PTR(-ENOMEM);

	DirEntry *d = vfs_find(path);
	if (IS_ERR_OR_NULL(d)) {
		kfree(f);
		return (void *)d;
	}
	if (IS_ERR_OR_NULL(d->inode)) {
		kfree(f);
		return ERR_PTR(-EIO);
	}

	fop_generic_open(d->inode, f);
	int r = d->inode->fop.open(d->inode, f);
	if (r != 0) {
		kfree(f);
		return ERR_PTR(r);
	}
	return f;
}

void vfs_close(File *f)
{
	if (IS_ERR_OR_NULL(f))
		return;

	f->fop.close(f);
	kfree(f);
}

ssize_t vfs_readdir(File *file, void *buf, size_t size)
{
	if (file->fop.readdir == NULL)
		return -EINVAL;
	return file->fop.readdir(file, buf, size);
}

ssize_t vfs_read(File *file, void *buf, size_t size)
{
	if (file->fop.read == NULL)
		return -EINVAL;
	return file->fop.read(file, buf, size);
}

ssize_t vfs_write(File *file, void *buf, size_t size)
{
	if (file->fop.write == NULL)
		return -EINVAL;
	return file->fop.write(file, buf, size);
}

void vfs_init()
{
	list_init(&vfs.fs_head);
	list_init(&vfs.mount_head);
}

int fs_register(FileSystem *fs)
{
	list_init(&fs->fs_node);
	list_add(&vfs.fs_head, &fs->fs_node);
	return 0;
}

int vfs_mount(const string_view fs_name, string_view path, string_view dev, mode_t mode)
{
	FileSystem *fs = NULL;
	FileSystem *it;
	list_for_each_entry(it, &vfs.fs_head, fs_node) {
		if (str_compare(&fs_name, &it->name)) {
			fs = it;
			break;
		}
	}

	if (!fs)
		return -ENOENT;

	DirEntry *target = vfs_find(path);
	if (IS_ERR_OR_NULL(target))
		return PTR_ERR_OR(target, -ENOENT);
	if (target->mount)
		return -EBUSY;

	MountPoint *m = kzalloc(sizeof(MountPoint));
	if (!m)
		return -ENOMEM;
	list_init(&m->node);

	m->root = fs->mount(fs, dev, mode);
	if (IS_ERR_OR_NULL(m->root)) {
		int err = PTR_ERR_OR(m->root, -EIO);
		kfree(m);
		return err;
	}
	m->mount_on = target;
	target->mount = m;
	list_add(&vfs.mount_head, &m->node);
	return 0;
}

int vfs_mount_root(const string_view fs_name)
{
	// @TODO: unmount if exists

	FileSystem *it;
	list_for_each_entry(it, &vfs.fs_head, fs_node) {
		if (str_compare(&fs_name, &it->name)) {
			vfs.root = it->mount(it, STR_LIT(""), 0777);
			if (IS_ERR_OR_NULL(vfs.root))
				return PTR_ERR_OR(vfs.root, -EIO);
			return 0;
		}
	}
	return -ENOENT;
}

DirEntry *mount_nodev(FileSystem *fs, int flags, int (*fill_super)(SuperBlock*))
{
	SuperBlock *sb = kzalloc(sizeof(SuperBlock));
	if (!sb)
		return ERR_PTR(-ENOMEM);
	list_init(&sb->sb_node);
	int r = fill_super(sb);
	if (r != 0) {
		kfree(sb);
		return ERR_PTR(r);
	}
	if (IS_ERR_OR_NULL(sb->root)) {
		kfree(sb);
		return ERR_PTR(-EINVAL);
	}
	sb->mode = flags;

	list_add(&fs->sb_list, &sb->sb_node);

	return sb->root;
}

DirEntry *mount_bdev(FileSystem *fs, string_view dev_path, int flags, int (*fill_super)(SuperBlock*))
{
	SuperBlock *sb = kzalloc(sizeof(SuperBlock));
	if (!sb)
		return ERR_PTR(-ENOMEM);
	list_init(&sb->sb_node);

	File *dev = vfs_open(dev_path);
	if (IS_ERR_OR_NULL(dev))
		return (void *)dev;

	sb->dev = dev;

	int r = fill_super(sb);
	if (r != 0) {
		kfree(sb);
		return ERR_PTR(r);
	}
	if (IS_ERR_OR_NULL(sb->root)) {
		kfree(sb);
		return ERR_PTR(-EINVAL);
	}
	sb->mode = flags;

	list_add(&fs->sb_list, &sb->sb_node);

	return sb->root;
}

void vfs_free_readdir_entries(DirInfo *arr, size_t len)
{
	for (size_t i = 0; i < len; ++i)
		kfree(arr[i].name);
}

void vfs_free_readdir_entries_vm(AddressSpace *vm, DirInfo *arr, size_t len)
{
	for (size_t i = 0; i < len; ++i)
		vmunmap(vm, arr[i].name);
}

DirEntry *make_dentry(string_view name, INode *inode, DirEntry *parent)
{
	DirEntry *dentry = kzalloc(sizeof(DirEntry));
	if (!dentry)
		return ERR_PTR(-ENOMEM);

	dentry->inode = inode;
	dentry->name = name;
	dentry->parent = parent;
	list_init(&dentry->node);
	list_init(&dentry->children);
	if (parent) {
		list_add(&parent->children, &dentry->node);
	}
	return dentry;
}

int fop_generic_open(struct INode *inode, struct File *file)
{
	file->fop = inode->fop;
	file->offset = 0;
	file->inode = inode;
	return 0;
}

int fop_stub_open(struct INode *inode, struct File *file)
{
	(void)inode;
	(void)file;
	return 0;
}

void fop_stub_close(struct File *file)
{
	(void)file;
}

ssize_t fop_stub_write(struct File *file, void *buf, size_t size)
{
	(void)file;
	(void)buf;
	(void)size;
	return 0;
}

ssize_t fop_stub_read(struct File *file, void *buf, size_t size)
{
	(void)file;
	(void)buf;
	(void)size;
	return 0;
}

u64 fop_generic_seek(struct File *file, u64 offset, SeekFrom from)
{
	if (from == SeekFrom_Start)
		file->offset = offset;
	if (from == SeekFrom_Curr)
		file->offset += offset;
	return file->offset;
}

void vfs_fill_inode(INode *inode, u32 inum, INodeOps iops, FileOps fops, mode_t mode, SuperBlock *sb)
{
	inode->inum = inum;
	inode->op = iops;
	inode->fop = fops;
	inode->mode = mode;
	inode->sb = sb;
}


FileOps vfs_stub_fileops = {
	.open = fop_generic_open,
	.write = fop_stub_write,
	.read = fop_stub_read,
	.seek = fop_generic_seek,
};


