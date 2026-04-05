/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Anbo Peng
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/**
 * @file  anbo_list.h
 * @brief Anbo Kernel — Intrusive Doubly-Linked Circular List
 *
 * Designed after the Linux kernel list_head. Nodes are embedded within
 * host structures, requiring zero dynamic memory allocation.
 * All operations are O(1).
 *
 * Constraints: C99 / fully static memory / no malloc / no sprintf
 */

#ifndef ANBO_LIST_H
#define ANBO_LIST_H

#include <stddef.h>   /* offsetof */

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Node Definition                                                    */
/* ================================================================== */

/**
 * @struct Anbo_ListNode
 * @brief  Intrusive list node, embedded within user structures.
 */
typedef struct Anbo_ListNode {
    struct Anbo_ListNode *next;
    struct Anbo_ListNode *prev;
} Anbo_ListNode;

/* ================================================================== */
/*  Host Structure Recovery Macros                                     */
/* ================================================================== */

/**
 * @def   ANBO_CONTAINER_OF(ptr, type, member)
 * @brief Recover the host structure pointer from a member pointer.
 */
#define ANBO_CONTAINER_OF(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/**
 * @def   ANBO_LIST_ENTRY(ptr, type, member)
 * @brief  Semantic alias for ANBO_CONTAINER_OF.
 */
#define ANBO_LIST_ENTRY(ptr, type, member) \
    ANBO_CONTAINER_OF(ptr, type, member)

/* ================================================================== */
/*  Initialization                                                     */
/* ================================================================== */

/**
 * @def   ANBO_LIST_INIT(name)
 * @brief Compile-time initialization: head node's next/prev both point to itself (empty list).
 */
#define ANBO_LIST_INIT(name) { &(name), &(name) }

/**
 * @brief Runtime initialization of a list head.
 */
static inline void Anbo_List_Init(Anbo_ListNode *head)
{
    head->next = head;
    head->prev = head;
}

/* ================================================================== */
/*  Basic Operations                                                   */
/* ================================================================== */

/**
 * @brief Insert node between prev and next (internal helper).
 */
static inline void anbo_list_insert_between(Anbo_ListNode *node,
                                             Anbo_ListNode *prev,
                                             Anbo_ListNode *next)
{
    next->prev = node;
    node->next = next;
    node->prev = prev;
    prev->next = node;
}

/**
 * @brief Insert node after head (head-insert).
 */
static inline void Anbo_List_InsertHead(Anbo_ListNode *head,
                                         Anbo_ListNode *node)
{
    anbo_list_insert_between(node, head, head->next);
}

/**
 * @brief Insert node before head (tail-insert).
 */
static inline void Anbo_List_InsertTail(Anbo_ListNode *head,
                                         Anbo_ListNode *node)
{
    anbo_list_insert_between(node, head->prev, head);
}

/**
 * @brief Remove node from the list.
 */
static inline void Anbo_List_Remove(Anbo_ListNode *node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

/* ================================================================== */
/*  Queries                                                            */
/* ================================================================== */

/**
 * @brief Check whether the list is empty.
 */
static inline int Anbo_List_IsEmpty(const Anbo_ListNode *head)
{
    return (head->next == head);
}

/* ================================================================== */
/*  Traversal Macros                                                   */
/* ================================================================== */

/**
 * @def   ANBO_LIST_FOR_EACH(pos, head)
 * @brief Forward traversal (do NOT remove nodes during iteration).
 */
#define ANBO_LIST_FOR_EACH(pos, head) \
    for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)

/**
 * @def   ANBO_LIST_FOR_EACH_SAFE(pos, tmp, head)
 * @brief Safe traversal (allows removing the current node during iteration).
 */
#define ANBO_LIST_FOR_EACH_SAFE(pos, tmp, head) \
    for ((pos) = (head)->next, (tmp) = (pos)->next; \
         (pos) != (head); \
         (pos) = (tmp), (tmp) = (pos)->next)

/**
 * @def   ANBO_LIST_FOR_EACH_ENTRY(pos, head, type, member)
 * @brief Traverse and directly obtain the host structure pointer.
 */
#define ANBO_LIST_FOR_EACH_ENTRY(pos, head, type, member) \
    for ((pos) = ANBO_LIST_ENTRY((head)->next, type, member); \
         &(pos)->member != (head); \
         (pos) = ANBO_LIST_ENTRY((pos)->member.next, type, member))

/**
 * @def   ANBO_LIST_FOR_EACH_ENTRY_SAFE(pos, tmp, head, type, member)
 * @brief Safe traversal of host structures (allows removing nodes during iteration).
 */
#define ANBO_LIST_FOR_EACH_ENTRY_SAFE(pos, tmp, head, type, member) \
    for ((pos) = ANBO_LIST_ENTRY((head)->next, type, member), \
         (tmp) = ANBO_LIST_ENTRY((pos)->member.next, type, member); \
         &(pos)->member != (head); \
         (pos) = (tmp), \
         (tmp) = ANBO_LIST_ENTRY((pos)->member.next, type, member))

#ifdef __cplusplus
}
#endif

#endif /* ANBO_LIST_H */
