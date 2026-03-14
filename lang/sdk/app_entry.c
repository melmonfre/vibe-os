#include <lang/include/vibe_app_runtime.h>

#ifndef VIBE_APP_BUILD_NAME
#error "VIBE_APP_BUILD_NAME must be defined for external app builds"
#endif

#ifndef VIBE_APP_BUILD_HEAP_SIZE
#define VIBE_APP_BUILD_HEAP_SIZE 65536u
#endif

extern uint8_t __app_load_start[];
extern uint8_t __app_image_end[];
extern uint8_t __app_bss_end[];

static void vibe_app_debug_write(const char *text) {
    __asm__ volatile("int $0x80"
                     :
                     : "a"(11), "b"((int)(uintptr_t)text), "c"(0), "d"(0), "S"(0), "D"(0)
                     : "memory", "cc");
}

static void vibe_app_debug_vga(int row, const char *text) {
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    int index = row * 80;

    while (text && *text && index < (80 * 25)) {
        vga[index++] = (uint16_t)(0x2E00u | (uint8_t)*text++);
    }
}

/*
 * Keep the external app ABI entrypoint mechanically simple.
 * Tail-call optimization here makes the stack discipline harder to reason
 * about while the loader ABI is still being stabilized.
 */
__attribute__((noinline, optimize("O0")))
int vibe_app_entry(const struct vibe_app_context *ctx, int argc, char **argv) {
    int rc;

    vibe_app_debug_vga(22, "app: entry");
    vibe_app_debug_write("hello.app: entry\n");
    vibe_app_runtime_init(ctx);
    vibe_app_debug_vga(23, "app: runtime");
    vibe_app_debug_write("hello.app: runtime ok\n");
    rc = vibe_app_main(argc, argv);
    vibe_app_debug_vga(24, "app: main returned");
    vibe_app_debug_write("hello.app: main returned\n");
    return rc;
}

__attribute__((section(".app_header")))
const struct vibe_app_header g_vibe_app_header = {
    VIBE_APP_MAGIC,
    VIBE_APP_ABI_VERSION,
    (uint16_t)sizeof(struct vibe_app_header),
    0u,
    0u,
    0u,
    (uint32_t)VIBE_APP_BUILD_HEAP_SIZE,
    VIBE_APP_BUILD_NAME
};
