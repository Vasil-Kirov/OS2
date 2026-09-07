
#ifndef _KMALLOC_H
#define _KMALLOC_H
#include <kcommon.h>

void kinit_heap();
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void kfree(void *p_);


#endif

