/*
 * Minimal shim of the old liteos los_singly_list_pri.h (verbatim from
 * refs/liteos, plus the missing VOID return type on OsSinglyListAdd that
 * the original file lacked). Only the OS_SL_LIST type is consumed by the
 * BOX M2 port; the traversal macros come along for free.
 */
#ifndef LOS_SINGLY_LIST_PRI_H
#define LOS_SINGLY_LIST_PRI_H
#include "los_typedef.h"
#include "los_list.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OS_SL_LIST {
    struct OS_SL_LIST *pstNext;
} OS_SL_LIST;

LITE_OS_SEC_ALW_INLINE STATIC INLINE VOID OsSinglyListInit(OS_SL_LIST *list)
{
    list->pstNext = list;
}

LITE_OS_SEC_ALW_INLINE STATIC INLINE VOID OsSinglyListAdd(OS_SL_LIST *list, OS_SL_LIST *node)
{
    node->pstNext = list->pstNext;
    list->pstNext = node;
}

LITE_OS_SEC_ALW_INLINE STATIC INLINE VOID OsSinglyListHeadInsert(OS_SL_LIST *list, OS_SL_LIST *node)
{
    OsSinglyListAdd(list, node);
}

LITE_OS_SEC_ALW_INLINE STATIC INLINE VOID OsSinglyListDelete(OS_SL_LIST *node)
{
    OS_SL_LIST *prev = node;
    OS_SL_LIST *curr = node->pstNext;

    while ((curr != NULL) && (curr != node)) {
        prev = curr;
        curr = curr->pstNext;
    }

    if (curr != NULL) {
        prev->pstNext = curr->pstNext;
        curr->pstNext = NULL;
    }
}

LITE_OS_SEC_ALW_INLINE STATIC INLINE BOOL OsSinglyListEmpty(const OS_SL_LIST *list)
{
    return (BOOL)(list->pstNext == list);
}

#define OS_SL_LIST_ENTRY LOS_DL_LIST_ENTRY
#define OS_SL_LIST_FIRST(object) ((object)->pstNext)

#ifdef __cplusplus
}
#endif
#endif
