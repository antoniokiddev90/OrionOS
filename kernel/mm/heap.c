#include "heap.h"
#include "vmm.h"
#include "../x64/paging.h"
#include <stddef.h>

#define HEAP_BASE       0xFFFF900000000000ULL
#define HEAP_INIT_SIZE  (16ULL * 1024 * 1024)
#define HEAP_GROW_SIZE  (4ULL * 1024 * 1024)

typedef struct block {
    size_t size;
    int    used;
    struct block *next;
    struct block *prev;
} block_t;

static block_t *head;
static uint64_t heap_end;
static uint64_t heap_bytes;
static size_t   used_bytes;

static void split(block_t *b, size_t want) {
    if (b->size < want + sizeof(block_t) + 16) return;
    block_t *n = (block_t *)((uint8_t *)(b + 1) + want);
    n->size = b->size - want - sizeof(block_t);
    n->used = 0;
    n->next = b->next;
    n->prev = b;
    if (b->next) b->next->prev = n;
    b->next = n;
    b->size = want;
}

static void coalesce(block_t *b) {
    if (b->next && !b->next->used) {
        block_t *n = b->next;
        b->size += sizeof(block_t) + n->size;
        b->next = n->next;
        if (n->next) n->next->prev = b;
    }
    if (b->prev && !b->prev->used) {
        block_t *p = b->prev;
        p->size += sizeof(block_t) + b->size;
        p->next = b->next;
        if (b->next) b->next->prev = p;
    }
}

void heap_init(void) {
    uint64_t va = vmm_alloc_at(HEAP_BASE, HEAP_INIT_SIZE,
                               PTE_PRESENT | PTE_WRITE);
    if (!va) return;
    head = (block_t *)HEAP_BASE;
    head->size = HEAP_INIT_SIZE - sizeof(block_t);
    head->used = 0;
    head->next = NULL;
    head->prev = NULL;
    heap_end = HEAP_BASE + HEAP_INIT_SIZE;
    heap_bytes = HEAP_INIT_SIZE;
    used_bytes = 0;
}

static int grow(size_t need) {
    (void)need;
    uint64_t va = heap_end;
    if (!vmm_alloc_at(va, HEAP_GROW_SIZE, PTE_PRESENT | PTE_WRITE)) return -1;
    block_t *b = (block_t *)va;
    b->size = HEAP_GROW_SIZE - sizeof(block_t);
    b->used = 0;
    b->next = NULL;
    b->prev = NULL;
    block_t *last = head;
    while (last->next) last = last->next;
    last->next = b;
    b->prev = last;
    coalesce(last);
    heap_end += HEAP_GROW_SIZE;
    heap_bytes += HEAP_GROW_SIZE;
    return 0;
}

void *kmalloc(size_t size) {
    if (!size) return NULL;
    size = (size + 15) & ~(size_t)15;
    for (block_t *b = head; b; b = b->next) {
        if (!b->used && b->size >= size) {
            split(b, size);
            b->used = 1;
            used_bytes += b->size;
            return (void *)(b + 1);
        }
    }
    if (grow(size) != 0) return NULL;
    return kmalloc(size);
}

void *kcalloc(size_t n, size_t size) {
    size_t total = n * size;
    void *p = kmalloc(total);
    if (!p) return NULL;
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < total; ++i) b[i] = 0;
    return p;
}

void kfree(void *ptr) {
    if (!ptr) return;
    block_t *b = ((block_t *)ptr) - 1;
    if (!b->used) return;
    b->used = 0;
    used_bytes -= b->size;
    coalesce(b);
}

size_t heap_used(void)  { return used_bytes; }
size_t heap_total(void) { return (size_t)heap_bytes; }
