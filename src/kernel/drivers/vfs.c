#include "vfs.h"
#include "kcommon.h"
#include <errno.h>

VFS vfs;

INode *find_inode(INode *parent, const char *str)
{
	if (!parent)
		return NULL;
	if (parent->op.lookup) {
		INode *next = parent->op.lookup(parent, str);
		if(!next)
			return NULL;

		const char *nexts = str;
		while(*nexts && *nexts != '/')
			nexts++;
		if(*nexts == '/')
			nexts++;

		if(*nexts) {
			return find_inode(next, nexts);
		}
		else {
			return parent;
		}
	}
	return NULL;
}

int open(const char *str, u32 flags)
{
	assert(vfs.root->op.lookup);

	if (str[0] != '/')
		return -ENOENT;
	str++;
	INode *inode = vfs.root->op.lookup(vfs.root, str);
	if (!inode)
		return -ENOENT;
	return 0;
}

int read()
{
	return 0;
}

int write()
{
	return 0;
}

