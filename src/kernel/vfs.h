

#ifndef _VFS_H
#define _VFS_H

#include <kcommon.h>
#include <list.h>
#include <kmem.h>

typedef u16 mode_t;

#define S_IFMT  00170000
#define S_IFSOCK 0140000
#define S_IFLNK	 0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000

#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m)	(((m) & S_IFMT) == S_IFSOCK)


struct File;
struct INode;
struct FileSystem;
struct MountPoint;
struct DirEntry;

typedef struct {
	size_t name_len;
	char *name;
} DirInfo;

typedef struct DirEntry {
	string_view name;

	struct INode *inode;
	struct MountPoint *mount;
	struct DirEntry *parent;

	ListNode node;
	ListNode children;
} DirEntry;

typedef struct {
	DirEntry *root;
	void *private;
	ListNode sb_node;
	struct File *dev;
	mode_t mode;
} SuperBlock;

typedef struct FileSystem {
	string_view name;
	DirEntry *(*mount)(struct FileSystem *fs, string_view dev, int flags);
	bool requires_bdev;
	ListNode sb_list;
	ListNode fs_node;
} FileSystem;

typedef enum {
	SeekFrom_Start,
	SeekFrom_Curr,
} SeekFrom;

typedef struct {
	int (*open)(struct INode *inode, struct File *file);
	void (*close)(struct File *file);
	ssize_t (*write)(struct File *file, void *buf, size_t size);
	ssize_t (*read)(struct File *file, void *buf, size_t size);
	ssize_t (*readdir)(struct File *file, void *buf, size_t size);
	u64 (*seek)(struct File *file, u64 offset, SeekFrom from);
} FileOps;

typedef struct {
	DirEntry *(*create)(struct DirEntry *dir, const string_view *name, mode_t);
	struct DirEntry *(*lookup)(struct INode *inode, DirEntry *parent, const string_view *name);
	i64 (*get_size)(struct INode *inode);
} INodeOps;

typedef struct MountPoint {
	DirEntry *root;
	DirEntry *mount_on; // parent fs' direntry for the same path
	ListNode node;
} MountPoint;

typedef struct INode {
	u32 inum;
	INodeOps op;
	FileOps fop;
	mode_t mode;

	void *priv; // mainly used by chrdevices
	SuperBlock *sb;
} INode;

typedef struct File {
	FileOps fop;
	INode *inode;
	u64 offset;
	void *priv;
} File;

typedef struct {
	DirEntry *root;
	ListNode fs_head;
	ListNode mount_head;
} VFS;

extern FileOps vfs_stub_fileops;

int fop_generic_open(struct INode *inode, struct File *file);
int fop_stub_open(struct INode *inode, struct File *file);
void fop_stub_close(struct File *file);
ssize_t fop_stub_write(struct File *file, void *buf, size_t size);
ssize_t fop_stub_read(struct File *file, void *buf, size_t size);
u64 fop_generic_seek(struct File *file, u64 offset, SeekFrom from);

void vfs_init();
int vfs_setup_dev();

DirEntry *vfs_find(string_view path);
File *vfs_open(string_view path);
void vfs_close(File *f);
ssize_t vfs_read(File *file, void *buf, size_t size);
ssize_t vfs_readdir(File *file, void *buf, size_t size);
ssize_t vfs_write(File *file, void *buf, size_t size);

int vfs_mount_root(const string_view fs_name);
int vfs_mount(const string_view fs_name, string_view path, string_view dev, mode_t mode);
int fs_register(FileSystem *fs);
DirEntry *mount_nodev(FileSystem *fs, int flags, int (*fill_super)(SuperBlock*));
DirEntry *mount_bdev(FileSystem *fs, string_view dev, int flags, int (*fill_super)(SuperBlock*));
void vfs_fill_inode(INode *inode, u32 inum, INodeOps iops, FileOps fops, mode_t mode, SuperBlock *sb);
DirEntry *make_dentry(string_view name, INode *inode, DirEntry *parent);

void vfs_free_readdir_entries(DirInfo *arr, size_t len);
void vfs_free_readdir_entries_vm(AddressSpace *vm, DirInfo *arr, size_t len);

#endif // _VFS_H

