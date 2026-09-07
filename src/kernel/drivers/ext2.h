
#ifndef _EXT_2
#define _EXT_2

#include "vfs.h"
#include <kcommon.h>

#define EXT2_SIG 0xef53

#define EXT2_FEATURE_INCOMPAT_COMPRESSION 0x0001
#define EXT2_FEATURE_INCOMPAT_FILETYPE    0x0002
#define EXT2_FEATURE_INCOMPAT_RECOVER     0x0004
#define EXT2_FEATURE_INCOMPAT_JOURNAL_DEV 0x0008

#define EXT2_FEATURE_READ_ONLY_SPARSE_SB  0x0001
#define EXT2_FEATURE_READ_ONLY_64BIT_FSZ  0x0002
#define EXT2_FEATURE_READ_ONLY_BINTREE_DIR  0x0004

typedef struct {
	u32 inode;
	u16 size; // total of the entire struct and subfields
	u8 name_len_lo;
	u8 file_type_or_name_len_hi;
	u8 name[];
} Ext2DirEntry;
static_assert(offsetof(Ext2DirEntry, name) == 8);

enum Ext2INodeType {
	Ext2INode_FIFO = 0x1000,
	Ext2INode_ChrDev = 0x2000,
	Ext2INode_Dir = 0x4000,
	Ext2INode_BlkDev = 0x6000,
	Ext2INode_File = 0x8000,
	Ext2INode_SymLink = 0xA000,
	Ext2INode_Socket = 0xC000,
};

typedef struct {
    u16 mode;              // Type and permissions
    u16 uid;               // User ID

    u32 size_low;          // Lower 32 bits of size

    u32 atime;             // Last access time
    u32 ctime;             // Creation time
    u32 mtime;             // Last modification time
    u32 dtime;             // Deletion time

    u16 gid;               // Group ID
    u16 links_count;       // Hard link count
    u32 blocks;            // 512-byte sectors allocated
    u32 flags;             // Inode flags
    u32 osd1;              // OS dependent value #1
    u32 block[15];         // Block pointers:
                           // [0..11] direct
                           // [12] singly indirect
                           // [13] doubly indirect
                           // [14] triply indirect

    u32 generation;        // File version (NFS)
    u32 file_acl;          // Extended attribute block
    u32 size_high;         // Upper 32 bits of size (regular files)
    u32 fragment;          // Fragment block address
    u8 osd2[12];           // OS dependent value #2

	u8 ext[128];
} Ext2INodeBase;

typedef struct {
	Ext2INodeBase base;
	INode inode;
} Ext2INode;

static_assert(sizeof(Ext2INodeBase) == 256);

typedef struct {
	u32 block_usage_bitmap; // BlockID
	u32 inode_usage_bitmap; // BlockID
	u32 inode_table;        // BlockID
	u16 unalloced_blocks;
	u16 unalloced_inodes;
	u16 dir_count;
	u8 reserved[14];
} Ext2GroupDescriptor;

static_assert(sizeof(Ext2GroupDescriptor) == 32);

typedef struct {
	u8 *buf; // fs->block_size in len
	Ext2INode *dir;
	Ext2DirEntry *entry;
	u64 size;
	u64 offset;
	u32 cached_block;
} Ext2FindState;

typedef struct {
	u32 inode_count;
	u32 block_count;
	u32 su_block_count;
	u32 unalloced_blocks;
	u32 unalloced_inodes;
	u32 start_idx;
	u32 block_size_log2; // 1024 << n = block_size
	u32 fragment_size_log2; // 1024 << n = fragment_size
	u32 blocks_per_group;
	u32 fragments_per_group;
	u32 inodes_per_group;
	u32 last_mount_time; // POSIX time
	u32 last_write_time; // POSIX time
	u16 mounts_since_check;
	u16 mounts_per_check;
	u16 ext2_sig; // 0xef53
	u16 fs_state;
	u16 on_error;
	u16 ver_minor;
	u32 last_check; // POSIX time
	u32 forced_check_interval;
	u32 os_id; // From which its created
	u32 ver_major;
	u16 user_id_for_reserved_blocks;
	u16 group_id_for_reserved_blocks;

	// For major version >= 1
	u32 first_non_reserved_inode;
	u16 inode_struct_size;
	u16 superblock_block_group;
	u32 optional_features;
	u32 required_features;
	u32 write_required_features;
	u8 fs_id[16];
	u8 vol_name[16];
	u8 path_last_mount[64];
	u32 compression;
	u8 prealloc_file_blocks;
	u8 prealloc_dir_blocks;
	u16 reserved;
	u8 journal_id[16];
	u32 journal_inode;
	u32 journal_device;
	u32 orphan_inode_list_head;
} Ext2Superblock;

typedef struct {
	SuperBlock *fsb;
	Ext2Superblock sb;

	u32 block_size;
	u32 inode_size;

	u32 group_count;
	Ext2GroupDescriptor *groups;
} Ext2FS;

INode *ext2_new_inode(Ext2FS *fs, u32 inode_num);
int ext2_init();


#endif // _EXT_2

