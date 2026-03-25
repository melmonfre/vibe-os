#include <lang/include/vibe_app.h>
#include <stdint.h>

extern int vibe_app_main(int argc, char **argv);

static void vibe_app_boot_debug(const char *text) {
    __asm__ volatile("push %%eax\n\t"
                     "push %%ebx\n\t"
                     "push %%ecx\n\t"
                     "push %%edx\n\t"
                     "push %%esi\n\t"
                     "push %%edi\n\t"
                     "int $0x80\n\t"
                     "pop %%edi\n\t"
                     "pop %%esi\n\t"
                     "pop %%edx\n\t"
                     "pop %%ecx\n\t"
                     "pop %%ebx\n\t"
                     "pop %%eax"
                     :
                     : "a"(11), "b"((int)(uintptr_t)text), "c"(0), "d"(0), "S"(0), "D"(0)
                     : "memory", "cc");
}

__attribute__((noinline, optimize("O0")))
int vibe_app_entry(const struct vibe_app_context *ctx, int argc, char **argv) {
    (void)ctx;
    vibe_app_boot_debug("app: entry begin\n");
    return vibe_app_main(argc, argv);
}

__attribute__((section(".app_header")))
const struct vibe_app_header g_vibe_app_header = {
    VIBE_APP_MAGIC,
    VIBE_APP_ABI_VERSION,
    (uint16_t)sizeof(struct vibe_app_header),
    0u,
    0u,
    0u,
    0u,
    262144u,
    "lua"
};
