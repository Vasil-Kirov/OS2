

#ifndef _VFS_H
#define _VFS_H

#include "kcommon.h"

struct INode;

typedef struct {
} FileSystem;

typedef struct {
	int (*write)(struct INode *inode, void *buf, size_t size);
	int (*read)(struct INode *inode, void *buf, size_t size);
} FileOps;

typedef struct {
	struct INode *(*lookup)(struct INode *inode, string_view *name);
} DirOps;

typedef struct INode {
	u32 inum;
	FileOps op;
	DirOps dop;
	void *private; // fs specific
} INode;

typedef struct {
	INode *root;
} VFS;

#endif // _VFS_H

