#include "user.h"

typedef struct Block {
    u32 size;
    struct Block *next;
    struct Block *previous;
    int available;
} Block;

static Block *head, *tail;
static const u32 alignment = 16;
static const u32 max_size = 0x7fffffe0;

_Static_assert(sizeof(Block) == 16, "heap alignment");

static void merge_next(Block *block) {
    Block *next = block->next;
    if (!next || !next->available || (char *)(block + 1) + block->size != (char *)next)
        return;
    block->size += sizeof(Block) + next->size;
    block->next = next->next;
    if (block->next) block->next->previous = block;
    else tail = block;
}

static void split(Block *block, u32 size) {
    if (block->size - size < sizeof(Block) + alignment) return;
    Block *rest = (Block *)((char *)(block + 1) + size);
    rest->size = block->size - size - sizeof(Block);
    rest->available = 1;
    rest->previous = block;
    rest->next = block->next;
    if (rest->next) rest->next->previous = rest;
    else tail = rest;
    block->next = rest;
    block->size = size;
    merge_next(rest);
}

void *malloc(u32 size) {
    if (!size || size > max_size) return 0;
    size = (size + alignment - 1) & ~(alignment - 1);
    for (Block *block = head; block; block = block->next) {
        if (block->available && block->size >= size) {
            split(block, size);
            block->available = 0;
            return block + 1;
        }
    }

    u32 end = (u32)sbrk(0);
    if (end == (u32)-1) return 0;
    u32 padding = -end & (alignment - 1);
    char *memory = sbrk(padding + sizeof(Block) + size);
    if (memory == (void *)-1) return 0;
    Block *block = (Block *)(memory + padding);
    block->size = size;
    block->available = 0;
    block->next = 0;
    block->previous = tail;
    if (tail) tail->next = block;
    else head = block;
    tail = block;
    return block + 1;
}

void free(void *memory) {
    if (!memory) return;
    Block *block = (Block *)memory - 1;
    block->available = 1;
    merge_next(block);
    if (block->previous && block->previous->available) merge_next(block->previous);
}

void *calloc(u32 count, u32 size) {
    if (!count || size > max_size / count) return 0;
    u32 bytes = count * size;
    unsigned char *memory = malloc(bytes);
    if (memory) for (u32 i = 0; i < bytes; ++i) memory[i] = 0;
    return memory;
}

void *realloc(void *memory, u32 size) {
    if (!memory) return malloc(size);
    if (!size) {
        free(memory);
        return 0;
    }
    if (size > max_size) return 0;
    Block *block = (Block *)memory - 1;
    if (size <= block->size) {
        split(block, (size + alignment - 1) & ~(alignment - 1));
        return memory;
    }
    unsigned char *next = malloc(size);
    if (!next) return 0;
    for (u32 i = 0; i < block->size; ++i) next[i] = ((unsigned char *)memory)[i];
    free(memory);
    return next;
}
