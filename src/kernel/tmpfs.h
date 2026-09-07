
#ifndef _TMPFS_H
#define _TMPFS_H

#include <vfs.h>
#include <list.h>


#define TMPFS_DIRECT_PAGES (32)
#define TMPFS_INDIRECT_PAGES (32)
#define TMPFS_PAGE_PER_INDIRECT (PAGE_SIZE/sizeof(void*))
#define TMPFS_MAX_PAGES (TMPFS_DIRECT_PAGES+(TMPFS_INDIRECT_PAGES*TMPFS_PAGE_PER_INDIRECT))
typedef struct {
	size_t page_count;
	void *direct_pages[TMPFS_DIRECT_PAGES];
	void **indirect_pages[TMPFS_INDIRECT_PAGES]; // pages of pages
} TmpFSPageTable;

typedef struct {
	INode base;
	string_view name;

	TmpFSPageTable page_table;

	ListNode node;
	ListNode children;
} TmpFSINode;

typedef struct {
	u32 inode_idx;
} TmpFSSBInfo;

INode *tmpfs_new_inode(TmpFSINode *parent, SuperBlock *block, mode_t mode);
int tmpfs_init();

#endif

