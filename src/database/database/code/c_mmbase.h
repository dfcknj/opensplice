/*
 *                         OpenSplice DDS
 *
 *   This software and documentation are Copyright 2006 to 2009 PrismTech
 *   Limited and its licensees. All rights reserved. See file:
 *
 *                     $OSPL_HOME/LICENSE
 *
 *   for full copyright notice and license terms.
 *
 */

#ifndef C_MM_H
#define C_MM_H

#include "c_typebase.h"

#if defined (__cplusplus)
extern "C" {
#endif

#ifdef OSPL_BUILD_DB
#define OS_API OS_API_EXPORT
#else
#define OS_API OS_API_IMPORT
#endif
/* !!!!!!!!NOTE From here no more includes are allowed!!!!!!! */

typedef struct c_mm_s       *c_mm;
typedef struct c_mmStatus_s c_mmStatus;

struct c_mmStatus_s {
    c_long size;
    c_long used;
    c_long maxUsed;
    c_long garbage;
    c_long count;
    c_long fails;
    /* The cached field will be filled with the amount of memory allocated for
     * caches (including all headers). */
    c_long cached;
    /* The preallocated field will be filled with the amount of memory that is
     * preallocated in caches, but is not in use. So in order to retain the
     * total amount of memory in use:
     *      totalInUse = used - preallocated;
     * And in order to get all free memory (including allocated, but available
     * in caches):
     *      totalFree = size - totalInUse */
    c_long preallocated;
};

OS_API c_mm c_mmCreate (void *address, c_long size);
OS_API c_mmStatus c_mmState (c_mm mm, c_bool fillPreAlloc);
OS_API c_mmStatus c_mmMapState (c_mm mm);
OS_API c_mmStatus c_mmListState (c_mm mm);

void  c_mmDestroy (c_mm mm);
void *c_mmAddress (c_mm mm);

void *c_mmMalloc  (c_mm mm, c_long   size);
void  c_mmFree    (c_mm mm, void *memory);

void *c_mmMallocCache  (c_mm mm, c_long   size);
void  c_mmFreeCache    (c_mm mm, void *memory);

void *c_mmBind    (c_mm mm, const c_char *name, void *memory);
void  c_mmUnbind  (c_mm mm, const c_char *name);
void *c_mmLookup  (c_mm mm, const c_char *name);

typedef void (*c_mmOutOfMemoryAction) (c_voidp arg);

void  c_mmOnOutOfMemory(c_mm mm, c_mmOutOfMemoryAction action, c_voidp arg);

#undef OS_API

#if defined (__cplusplus)
}
#endif

#endif /* C_MM_H */
