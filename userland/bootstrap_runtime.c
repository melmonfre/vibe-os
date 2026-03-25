#include <kernel/memory/heap.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct bootstrap_heap_block {
    size_t size;
    int free;
    struct bootstrap_heap_block *next;
};

static struct bootstrap_heap_block *g_bootstrap_heap = 0;
static int g_bootstrap_heap_ready = 0;

static void bootstrap_heap_init(void) {
    size_t free_bytes;
    size_t target;
    struct bootstrap_heap_block *head;

    if (g_bootstrap_heap_ready) {
        return;
    }

    free_bytes = kernel_heap_free();
    target = 2u * 1024u * 1024u;
    if (free_bytes > 16u * 1024u * 1024u) {
        target = 8u * 1024u * 1024u;
    } else if (free_bytes > 4u * 1024u * 1024u) {
        target = 4u * 1024u * 1024u;
    }

    head = (struct bootstrap_heap_block *)kernel_malloc(target);
    if (!head) {
        g_bootstrap_heap_ready = 1;
        return;
    }

    head->size = target - sizeof(*head);
    head->free = 1;
    head->next = 0;
    g_bootstrap_heap = head;
    g_bootstrap_heap_ready = 1;
}

static void *bootstrap_block_payload(struct bootstrap_heap_block *block) {
    return (void *)(block + 1);
}

static struct bootstrap_heap_block *bootstrap_payload_block(void *ptr) {
    return ((struct bootstrap_heap_block *)ptr) - 1;
}

static void bootstrap_heap_split(struct bootstrap_heap_block *block, size_t size) {
    struct bootstrap_heap_block *next;

    if (!block || block->size <= size + sizeof(*next) + 8u) {
        return;
    }

    next = (struct bootstrap_heap_block *)((uint8_t *)bootstrap_block_payload(block) + size);
    next->size = block->size - size - sizeof(*next);
    next->free = 1;
    next->next = block->next;
    block->size = size;
    block->next = next;
}

static void bootstrap_heap_coalesce(void) {
    struct bootstrap_heap_block *block = g_bootstrap_heap;

    while (block && block->next) {
        if (block->free && block->next->free) {
            block->size += sizeof(*block) + block->next->size;
            block->next = block->next->next;
            continue;
        }
        block = block->next;
    }
}

void *malloc(size_t size) {
    struct bootstrap_heap_block *block;

    if (size == 0u) {
        return 0;
    }

    bootstrap_heap_init();
    if (!g_bootstrap_heap) {
        return 0;
    }

    if (size & 7u) {
        size += 8u - (size & 7u);
    }

    block = g_bootstrap_heap;
    while (block) {
        if (block->free && block->size >= size) {
            bootstrap_heap_split(block, size);
            block->free = 0;
            return bootstrap_block_payload(block);
        }
        block = block->next;
    }

    return 0;
}

void free(void *ptr) {
    if (!ptr) {
        return;
    }

    bootstrap_payload_block(ptr)->free = 1;
    bootstrap_heap_coalesce();
}

void *realloc(void *ptr, size_t size) {
    struct bootstrap_heap_block *block;
    void *new_ptr;

    if (!ptr) {
        return malloc(size);
    }

    if (size == 0u) {
        free(ptr);
        return 0;
    }

    block = bootstrap_payload_block(ptr);
    if (block->size >= size) {
        return ptr;
    }

    new_ptr = malloc(size);
    if (!new_ptr) {
        return 0;
    }

    memcpy(new_ptr, ptr, block->size);
    free(ptr);
    return new_ptr;
}
