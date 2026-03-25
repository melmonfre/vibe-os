#ifndef KERNEL_MICROKERNEL_LAUNCH_H
#define KERNEL_MICROKERNEL_LAUNCH_H

#include <stdint.h>

struct process;

#define MK_LAUNCH_ABI_VERSION 1u
#define MK_LAUNCH_NAME_MAX 16u
#define MK_LAUNCH_SLOTS 16u
#define MK_LAUNCH_STACK_SIZE_DEFAULT 4096u

enum mk_launch_kind {
    MK_LAUNCH_KIND_NONE = 0,
    MK_LAUNCH_KIND_DRIVER = 1,
    MK_LAUNCH_KIND_SERVICE = 2
};

enum mk_launch_flags {
    MK_LAUNCH_FLAG_BOOTSTRAP = 1u << 0,
    MK_LAUNCH_FLAG_CRITICAL = 1u << 1,
    MK_LAUNCH_FLAG_BUILTIN = 1u << 2
};

struct mk_launch_descriptor {
    uint32_t abi_version;
    uint32_t kind;
    uint32_t service_type;
    uint32_t flags;
    uint32_t stack_size;
    char name[MK_LAUNCH_NAME_MAX];
    void (*entry)(void);
};

struct mk_launch_context {
    uint32_t abi_version;
    uint32_t pid;
    uint32_t kind;
    uint32_t service_type;
    uint32_t flags;
    uint32_t boot_flags;
    uint32_t boot_partition_lba;
    uint32_t boot_partition_sectors;
    uint32_t data_partition_lba;
    uint32_t data_partition_sectors;
    char name[MK_LAUNCH_NAME_MAX];
};

void mk_launch_init(void);
int mk_launch_validate_descriptor(const struct mk_launch_descriptor *descriptor);
int mk_launch_bootstrap(const struct mk_launch_descriptor *descriptor);
void mk_launch_release_pid(int pid);
const struct mk_launch_context *mk_launch_context_for_pid(int pid);
const struct mk_launch_context *mk_launch_context_current(void);

#endif
