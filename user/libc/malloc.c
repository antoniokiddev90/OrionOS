#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>

#define MALLOC_ALIGNMENT 16
#define MALLOC_ARENA_SIZE (256UL * 1024)
#define MALLOC_LARGE_THRESHOLD (64UL * 1024)
#define MALLOC_MIN_SPLIT 32

struct chunk {
    size_t size;
    size_t flags;
    struct chunk *next_free;
    struct chunk *previous_free;
};

#define CHUNK_HEADER_SIZE 16
#define CHUNK_USED 1UL
#define CHUNK_LARGE 2UL

struct arena {
    uint8_t *start;
    size_t size;
    struct arena *next;
};

static struct arena *arenas;
static struct chunk *free_list;

static size_t align_up(size_t value) {
    return (value + MALLOC_ALIGNMENT - 1) & ~(size_t)(MALLOC_ALIGNMENT - 1);
}

static struct chunk *chunk_from_pointer(void *pointer) {
    return (struct chunk *)((uint8_t *)pointer - CHUNK_HEADER_SIZE);
}

static void *pointer_from_chunk(struct chunk *chunk) {
    return (uint8_t *)chunk + CHUNK_HEADER_SIZE;
}

static void free_list_insert(struct chunk *chunk) {
    chunk->previous_free = NULL;
    chunk->next_free = free_list;
    if (free_list) free_list->previous_free = chunk;
    free_list = chunk;
}

static void free_list_remove(struct chunk *chunk) {
    if (chunk->previous_free) chunk->previous_free->next_free = chunk->next_free;
    else free_list = chunk->next_free;
    if (chunk->next_free) chunk->next_free->previous_free = chunk->previous_free;
    chunk->next_free = NULL;
    chunk->previous_free = NULL;
}

static struct arena *arena_containing(struct chunk *chunk) {
    for (struct arena *arena = arenas; arena; arena = arena->next) {
        uint8_t *start = arena->start;
        uint8_t *end = arena->start + arena->size;
        if ((uint8_t *)chunk >= start && (uint8_t *)chunk < end) return arena;
    }
    return NULL;
}

static struct chunk *next_chunk_in_arena(struct arena *arena, struct chunk *chunk) {
    uint8_t *next = (uint8_t *)chunk + chunk->size;
    if (next >= arena->start + arena->size) return NULL;
    return (struct chunk *)next;
}

static void *map_memory(size_t size) {
    void *result = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return result == MAP_FAILED ? NULL : result;
}

static struct arena *arena_create(size_t size) {
    uint8_t *region = map_memory(size + align_up(sizeof(struct arena)));
    if (!region) return NULL;
    struct arena *arena = (struct arena *)region;
    arena->start = region + align_up(sizeof(struct arena));
    arena->size = size;
    arena->next = arenas;
    arenas = arena;
    struct chunk *initial = (struct chunk *)arena->start;
    initial->size = size;
    initial->flags = 0;
    initial->next_free = NULL;
    initial->previous_free = NULL;
    free_list_insert(initial);
    return arena;
}

static struct chunk *find_fit(size_t needed) {
    for (struct chunk *chunk = free_list; chunk; chunk = chunk->next_free) {
        if (chunk->size >= needed) return chunk;
    }
    return NULL;
}

static void split_chunk(struct chunk *chunk, size_t needed) {
    if (chunk->size < needed + CHUNK_HEADER_SIZE + MALLOC_MIN_SPLIT) return;
    struct chunk *remainder = (struct chunk *)((uint8_t *)chunk + needed);
    remainder->size = chunk->size - needed;
    remainder->flags = 0;
    remainder->next_free = NULL;
    remainder->previous_free = NULL;
    chunk->size = needed;
    free_list_insert(remainder);
}

static void *allocate_large(size_t size) {
    size_t total = align_up(size + CHUNK_HEADER_SIZE);
    struct chunk *chunk = map_memory(total);
    if (!chunk) return NULL;
    chunk->size = total;
    chunk->flags = CHUNK_USED | CHUNK_LARGE;
    chunk->next_free = NULL;
    chunk->previous_free = NULL;
    return pointer_from_chunk(chunk);
}

static void coalesce(struct chunk *chunk) {
    struct arena *arena = arena_containing(chunk);
    if (!arena) return;
    struct chunk *next = next_chunk_in_arena(arena, chunk);
    if (next && !(next->flags & CHUNK_USED)) {
        free_list_remove(next);
        chunk->size += next->size;
    }
    if ((uint8_t *)chunk > arena->start) {
        struct chunk *scan = (struct chunk *)arena->start;
        while ((uint8_t *)scan < (uint8_t *)chunk) {
            struct chunk *candidate = scan;
            struct chunk *after = next_chunk_in_arena(arena, scan);
            if (after == chunk && !(candidate->flags & CHUNK_USED)) {
                free_list_remove(candidate);
                candidate->size += chunk->size;
                free_list_insert(candidate);
                return;
            }
            if (!after) break;
            scan = after;
        }
    }
}

void *malloc(size_t size) {
    if (!size) return NULL;
    if (size >= MALLOC_LARGE_THRESHOLD) return allocate_large(size);
    size_t needed = align_up(size + CHUNK_HEADER_SIZE);
    struct chunk *chunk = find_fit(needed);
    if (!chunk) {
        if (!arena_create(MALLOC_ARENA_SIZE)) return NULL;
        chunk = find_fit(needed);
        if (!chunk) return NULL;
    }
    free_list_remove(chunk);
    split_chunk(chunk, needed);
    chunk->flags = CHUNK_USED;
    return pointer_from_chunk(chunk);
}

void free(void *pointer) {
    if (!pointer) return;
    struct chunk *chunk = chunk_from_pointer(pointer);
    if (chunk->flags & CHUNK_LARGE) {
        munmap(chunk, chunk->size);
        return;
    }
    if (!(chunk->flags & CHUNK_USED)) return;
    chunk->flags = 0;
    coalesce(chunk);
    if (!(chunk->flags & CHUNK_USED)) free_list_insert(chunk);
}
