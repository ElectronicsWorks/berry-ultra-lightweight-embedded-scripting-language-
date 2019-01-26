#include "be_mem.h"
#include "berry.h"
#include <stdlib.h>

#define os_malloc(size)             malloc(size)
#define os_free(ptr)                free(ptr)
#define os_realloc(ptr, size)       realloc(ptr, size)

#define addr_pos(a, p, o)   ((void*)((size_t)(a) o (p)))
#define addr_region(a, s)   addr_pos((a), sizeof(s), +)
#define addr_base(a, s)     addr_pos((a), sizeof(s), -)
#define data_size(s)        ((uint16_t)((s) & 0xFFFC))
#define node_size(s)        (data_size(s) + sizeof(mnode))
#define prev_ptr(node)      addr_pos(node, node_size((node)->prev), -)
#define next_ptr(node)      addr_pos(node, node_size((node)->size), +)
#define isused(node)        ((node)->size & 1)

#define inheap(heap, ptr) \
    ((ptr) > (void*)(heap) && (ptr) < addr_pos((heap), (heap)->size, +))

typedef struct {
    uint16_t prev;
    uint16_t size;
} mnode;

typedef struct mheap {
    uint16_t use, size;
    mnode *lastuse;
    struct mheap *next;
} mheap;

typedef struct {
    mheap *heap;
    size_t use;
} memlist;

typedef struct {
    size_t size;
} mdesc;

static memlist mlist = { .heap = NULL, .use = 0 };

#if BE_USE_SOBJ_HEAP
static void* __malloc(memlist *m, size_t size);
static void __free(memlist *m, void *ptr);
static void* __realloc(memlist *m, void *ptr, size_t size);
#endif

static void* l_malloc(size_t size)
{
    mdesc *obj = os_malloc(size + sizeof(mdesc));

    if (obj != NULL) {
        obj->size = size;
        mlist.use += size;
        return addr_region(obj, mdesc);
    }
    return NULL;
}

static void l_free(void *ptr)
{
    if (ptr != NULL) {
        mdesc *obj = addr_base(ptr, mdesc);
        mlist.use -= obj->size;
        os_free(obj);
    }
}

static void* l_realloc(void *ptr, size_t size)
{
    if (ptr != NULL) {
        mdesc *obj, *old = addr_base(ptr, mdesc);
        mlist.use = mlist.use + size - old->size;
        obj = (mdesc*)os_realloc(old, size + sizeof(mdesc));
        if (obj != NULL) {
            obj->size = size;
            return addr_region(obj, mdesc);
        }
        os_free(old);
        return NULL;
    }
    return be_malloc(size);
}

#if BE_USE_SOBJ_HEAP
static mheap* heap_insert(memlist *m, size_t size)
{
    mheap *heap = os_malloc(size);
    if (heap) {
        mnode *node = addr_region(heap, mheap);
        heap->size = (uint16_t)size;
        heap->use = 0;
        heap->next = m->heap ? m->heap : NULL;
        heap->lastuse = node;
        node->size = data_size(size - sizeof(mheap));
        node->prev = 0;
        m->heap = heap;
    }
    return heap;
}

static void free_empty_heap(memlist *m)
{
    mheap *last, *this, *next;
    for (last = this = m->heap; this; this = next) {
        next = this->next;
        if (this->use == 0) { /* is empty heap */
            if (this == m->heap) { /* first node */
                m->heap = next;
                last = next;
            } else { /* other */
                last->next = next;
            }
            os_free(this);
        } else {
            last = this;
        }
    }
}

static void node_split(mheap *heap, mnode *node, size_t size)
{
    size_t tail = node->size - size - sizeof(mnode);
    mnode *next = next_ptr(node);
    if (next < (mnode*)addr_pos(heap, heap->size, +)) {
        next->prev = data_size(tail); /* link next node */
    }
    node->size = data_size(size) | 1; /* mark used */
    next = next_ptr(node);  /* split */
    next->size = data_size(tail);
    next->prev = data_size(size);
}

static void* node_alloc(memlist *m, mheap *heap, size_t size)
{
    mnode *node = heap->lastuse, *next = NULL;
    mnode *end = addr_pos(heap, heap->size, +);
    for (; next != node; node = next_ptr(node)) {
        next = next_ptr(node);
        if (next >= end) {
            next = addr_region(heap, mheap);   
        }
        if (node->size >= size && !isused(node)) {
            if (node->size - size > sizeof(mnode)) {
                node_split(heap, node, size); /* split node */
            } else {
                node->size |= 1; /* mark used */
            }
            heap->lastuse = node;
            heap->use += data_size(node->size);
            m->use += data_size(node->size);
            return addr_region(node, mnode);
        }
    }
    return NULL;
}

static void node_free(memlist *m, mheap *heap, void *ptr)
{
    mnode *node = addr_base(ptr, mnode);
    mnode *prev = prev_ptr(node);
    mnode *next = next_ptr(node);
    mnode *end = addr_pos(heap, heap->size, +);
    node->size = data_size(node->size);
    heap->lastuse = node;
    heap->use -= node->size;
    m->use -= node->size;
    /* splicing the free block behind */
    if (next < end && !isused(next)) {
        /* merge size with tail */
        node->size = node->size + node_size(next->size);
        next = next_ptr(node); /* update next node */
        if (next < end) {
            next->prev = node->size;
        }
    }
    /* splicing the free block in front */
    if (node->prev && !isused(prev)) {
        /* merge size with previous */
        prev->size = prev->size + node_size(node->size);
        heap->lastuse = prev;
        next = next_ptr(prev); /* update next node */
        if (next < end) {
            next->prev = data_size(prev->size);
        }
    }
}

static void* node_realloc(memlist *m, mheap *heap, void *ptr, size_t size)
{
    size_t tail;
    mnode *node = addr_base(ptr, mnode);
    if (data_size(node->size) < size) {
        void *data = __malloc(m, size);
        memcpy(data, ptr, data_size(node->size));
        node_free(m, heap, ptr);
        return data;
    }
    /* split node */
    tail = data_size(node->size) - size;
    if (tail > sizeof(mnode)) { /* split node */
        node_split(heap, node, size);
        heap->lastuse = node;
        heap->use -= (uint16_t)tail;
        m->use -= tail;
    }
    return ptr;
}

static size_t align_size(size_t size)
{
    size_t mod = size & (sizeof(size_t) - 1);
    if (mod || size == 0) {
        size += sizeof(size_t) - mod;
    }
    return size;
}

static void* __malloc(memlist *m, size_t size)
{
    size = align_size(size);
    if (size <= BE_SOBJ_SIZE_MAX) {
        void *data = NULL;
        mheap *heap = m->heap;
        for (; heap && data; heap = heap->next) {
            if ((size_t)(heap->size - heap->use)
                > sizeof(mheap) + sizeof(mnode)) {
                data = node_alloc(m, heap, size);
                if (data != NULL) {
                    return data;
                }
            }
        }
        heap = heap_insert(m, BE_SOBJ_HEAP_SIZE);
        return heap ? node_alloc(m, heap, size) : NULL;
    }
    return l_malloc(size);
}

static void __free(memlist *m, void *ptr)
{
    mheap *heap = m->heap;
    for (; heap; heap = heap->next) {
        if (inheap(heap, ptr)) {
            node_free(m, heap, ptr);
            break;
        }
    }
    free_empty_heap(m);
    if (heap == NULL) {
        l_free(ptr);
    }
}

static void* __realloc(memlist *m, void *ptr, size_t size)
{
    mheap *heap = m->heap;
    size = align_size(size);
    for (; heap; heap = heap->next) {
        if (inheap(heap, ptr)) {
            return node_realloc(m, heap, ptr, size);
        }
    }
    return l_realloc(ptr, size);
}
#endif

void* be_malloc(size_t size)
{
#if BE_USE_SOBJ_HEAP
    return __malloc(&mlist, size);
#else
    return l_malloc(size);
#endif
}

void be_free(void *ptr)
{
#if BE_USE_SOBJ_HEAP
    __free(&mlist, ptr);
#else
    l_free(ptr);
#endif
}

void* be_realloc(void *ptr, size_t size)
{
#if BE_USE_SOBJ_HEAP
    return __realloc(&mlist, ptr, size);
#else
    return l_realloc(ptr, size);
#endif
}

size_t be_mcount(void)
{
    return mlist.use;
}
