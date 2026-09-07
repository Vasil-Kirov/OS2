
#ifndef _LIST_H
#define _LIST_H

#include <kcommon.h>

typedef struct ListNode {
	struct ListNode *prev;
	struct ListNode *next;
} ListNode;

static inline bool list_valid(ListNode *node)
{
	return node && node->prev && node->next && node->next->prev == node;
}

static inline void list_init(ListNode *head)
{
	head->prev = head;
	head->next = head;
}

static inline bool list_empty(ListNode *head)
{
	return head->next == head;
}

static inline void list_add(ListNode *head, ListNode *new)
{
	if (!list_valid(head) || !list_valid(new))
		return;

	new->next = head->next;
	new->prev = head;
	head->next->prev = new;
	head->next = new;
}

static inline void list_remove(ListNode *node)
{
	node->prev->next = node->next;
	node->next->prev = node->prev;
}

#define list_next_entry(e, member) container_of(e->member.next, typeof(*e), member)

#define list_for_each_entry(it, head, member) \
	for (it = container_of((head)->next, typeof(*it), member); \
			&it->member != (head); \
			it = list_next_entry(it, member))

#define list_for_each_entry_safe(pos, n, head, member) \
	for (pos = container_of((head)->next, typeof(*it), member), n = list_next_entry(pos, member); \
			&it->member != (head);			\
			pos = n, n = list_next_entry(n, member))


#endif

