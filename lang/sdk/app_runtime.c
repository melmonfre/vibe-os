#include <lang/include/vibe_app_runtime.h>

#define VIBE_APP_SYSCALL_INPUT_KEY 5
#define VIBE_APP_SYSCALL_YIELD 10
#define VIBE_APP_SYSCALL_WRITE_DEBUG 11
#define VIBE_APP_SYSCALL_TEXT_PUTC 12

struct heap_block {
    size_t size;
    int free;
    struct heap_block *next;
};

static const struct vibe_app_context *g_app_ctx = 0;
static struct heap_block *g_heap_head = 0;

static int vibe_app_syscall5(int num, int a, int b, int c, int d, int e) {
    int ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    return ret;
}

static void heap_init(void *base, size_t size) {
    g_heap_head = 0;
    if (base == 0 || size <= sizeof(struct heap_block)) {
        return;
    }

    g_heap_head = (struct heap_block *)base;
    g_heap_head->size = size - sizeof(struct heap_block);
    g_heap_head->free = 1;
    g_heap_head->next = 0;
}

static void *block_payload(struct heap_block *block) {
    return (void *)(block + 1);
}

static struct heap_block *payload_block(void *ptr) {
    return ((struct heap_block *)ptr) - 1;
}

static void split_block(struct heap_block *block, size_t size) {
    struct heap_block *next;

    if (block->size <= size + sizeof(struct heap_block) + 8u) {
        return;
    }

    next = (struct heap_block *)((uint8_t *)block_payload(block) + size);
    next->size = block->size - size - sizeof(struct heap_block);
    next->free = 1;
    next->next = block->next;
    block->size = size;
    block->next = next;
}

static void coalesce_blocks(void) {
    struct heap_block *block = g_heap_head;

    while (block && block->next) {
        if (block->free && block->next->free) {
            block->size += sizeof(struct heap_block) + block->next->size;
            block->next = block->next->next;
        } else {
            block = block->next;
        }
    }
}

const struct vibe_app_context *vibe_app_get_context(void) {
    return g_app_ctx;
}

void vibe_app_runtime_init(const struct vibe_app_context *ctx) {
    g_app_ctx = ctx;
    if (ctx) {
        heap_init(ctx->heap_base, ctx->heap_size);
    } else {
        g_heap_head = 0;
    }
}

void vibe_app_console_putc(char c) {
    (void)vibe_app_syscall5(VIBE_APP_SYSCALL_TEXT_PUTC, (int)(unsigned char)c, 0, 0, 0, 0);
}

void vibe_app_console_write(const char *text) {
    if (text == 0) {
        return;
    }
    while (*text != '\0') {
        vibe_app_console_putc(*text++);
    }
}

int vibe_app_poll_key(void) {
    return vibe_app_syscall5(VIBE_APP_SYSCALL_INPUT_KEY, 0, 0, 0, 0, 0);
}

void vibe_app_yield(void) {
    (void)vibe_app_syscall5(VIBE_APP_SYSCALL_YIELD, 0, 0, 0, 0, 0);
}

int vibe_app_read_file(const char *path, const char **data_out, int *size_out) {
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->read_file) {
        return -1;
    }
    return g_app_ctx->host->read_file(path, data_out, size_out);
}

int vibe_app_read_line(char *buf, int max_len, const char *prompt) {
    int len = 0;

    if (!buf || max_len <= 0) {
        return -1;
    }

    if (prompt) {
        vibe_app_console_write(prompt);
    }

    for (;;) {
        int c = vibe_app_poll_key();

        if (c == 0) {
            vibe_app_yield();
            continue;
        }

        if (c == '\r') {
            c = '\n';
        }

        if (c == 3) {
            buf[0] = '\0';
            vibe_app_console_write("^C\n");
            return -1;
        }

        if (c == '\n') {
            buf[len] = '\0';
            vibe_app_console_putc('\n');
            return len;
        }

        if ((c == '\b' || c == 127) && len > 0) {
            --len;
            buf[len] = '\0';
            vibe_app_console_write("\b \b");
            continue;
        }

        if (c >= 32 && c < 127 && len + 1 < max_len) {
            buf[len++] = (char)c;
            buf[len] = '\0';
            vibe_app_console_putc((char)c);
        }
    }
}

void *vibe_app_malloc(size_t size) {
    struct heap_block *block;

    if (size == 0u) {
        return 0;
    }
    if (size & 7u) {
        size += 8u - (size & 7u);
    }

    block = g_heap_head;
    while (block) {
        if (block->free && block->size >= size) {
            split_block(block, size);
            block->free = 0;
            return block_payload(block);
        }
        block = block->next;
    }

    if (g_app_ctx && g_app_ctx->host && g_app_ctx->host->write_debug) {
        g_app_ctx->host->write_debug("vibe_app_malloc: out of memory\n");
    } else {
        (void)vibe_app_syscall5(VIBE_APP_SYSCALL_WRITE_DEBUG,
                                (int)(uintptr_t)"vibe_app_malloc: out of memory\n",
                                0,
                                0,
                                0,
                                0);
    }
    return 0;
}

void vibe_app_free(void *ptr) {
    if (!ptr) {
        return;
    }
    payload_block(ptr)->free = 1;
    coalesce_blocks();
}

void *vibe_app_realloc(void *ptr, size_t size) {
    void *new_ptr;
    struct heap_block *block;

    if (ptr == 0) {
        return vibe_app_malloc(size);
    }
    if (size == 0u) {
        vibe_app_free(ptr);
        return 0;
    }

    block = payload_block(ptr);
    if (block->size >= size) {
        return ptr;
    }

    new_ptr = vibe_app_malloc(size);
    if (!new_ptr) {
        return 0;
    }
    memcpy(new_ptr, ptr, block->size);
    vibe_app_free(ptr);
    return new_ptr;
}

void *malloc(size_t size) {
    return vibe_app_malloc(size);
}

void free(void *ptr) {
    vibe_app_free(ptr);
}

void *realloc(void *ptr, size_t size) {
    return vibe_app_realloc(ptr, size);
}

void *memcpy(void *dst, const void *src, size_t size) {
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    size_t i;

    for (i = 0; i < size; ++i) {
        out[i] = in[i];
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t size) {
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;

    if (out < in) {
        return memcpy(dst, src, size);
    }

    while (size > 0u) {
        --size;
        out[size] = in[size];
    }
    return dst;
}

void *memset(void *dst, int value, size_t size) {
    uint8_t *out = (uint8_t *)dst;
    size_t i;

    for (i = 0; i < size; ++i) {
        out[i] = (uint8_t)value;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t size) {
    const uint8_t *lhs = (const uint8_t *)a;
    const uint8_t *rhs = (const uint8_t *)b;
    size_t i;

    for (i = 0; i < size; ++i) {
        if (lhs[i] != rhs[i]) {
            return (int)lhs[i] - (int)rhs[i];
        }
    }
    return 0;
}

size_t strlen(const char *text) {
    size_t len = 0;

    while (text && text[len] != '\0') {
        ++len;
    }
    return len;
}

int strcmp(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return (unsigned char)*a - (unsigned char)*b;
        }
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t size) {
    size_t i;

    for (i = 0; i < size; ++i) {
        if (a[i] != b[i] || a[i] == '\0' || b[i] == '\0') {
            return (unsigned char)a[i] - (unsigned char)b[i];
        }
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    size_t i = 0;

    do {
        dst[i] = src[i];
    } while (src[i++] != '\0');
    return dst;
}

char *strchr(const char *text, int c) {
    while (*text != '\0') {
        if (*text == (char)c) {
            return (char *)text;
        }
        ++text;
    }
    return c == 0 ? (char *)text : 0;
}
