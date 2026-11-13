#ifndef KERNEL_MICROKERNEL_LAUNCH_H
#define KERNEL_MICROKERNEL_LAUNCH_H

#include <stdint.h>

struct process;

#define MK_LAUNCH_ABI_VERSION 2u
#define MK_LAUNCH_NAME_MAX 16u
#define MK_LAUNCH_ARGC_MAX 8u
#define MK_LAUNCH_ARGV_BYTES 192u
#define MK_LAUNCH_SLOTS 32u
/*
 * KNOWN_BUGS #27 (12/11): 4096 was a death sentence, not a size. A desktop
 * background helper launched via sys_launch_app_argv with no explicit
 * stack_size got exactly one page of KERNEL stack; the launch/service path
 * (trap frame + printf buffers + ELF read) touched the bottom repeatedly
 * (witnesses: `tick-guard: BOTTOM-TOUCHED pid=17 stack=... size=4096` spam,
 * and the neto GP panic where the VICTIM task pid=27 had stack=399e4358
 * size=4096, its base glued to the ksh grandchild's saved frames at
 * 399e31fc -- the overflow rewrote the neighbour's context, whose iret then
 * executed the neto's user window in ring0 (`GP err=ff50 eip=1cffdd66
 * cs=8`). 16 KiB matches the explicit stacks already used for hosts
 * (lang_loader 16384, service hosts 32768); internal allocation only, no
 * ABI surface.
 */
#define MK_LAUNCH_STACK_SIZE_DEFAULT 16384u

enum mk_launch_kind {
    MK_LAUNCH_KIND_NONE = 0,
    MK_LAUNCH_KIND_DRIVER = 1,
    MK_LAUNCH_KIND_SERVICE = 2,
    MK_LAUNCH_KIND_USER = 3
};

enum mk_launch_flags {
    MK_LAUNCH_FLAG_BOOTSTRAP = 1u << 0,
    MK_LAUNCH_FLAG_CRITICAL = 1u << 1,
    MK_LAUNCH_FLAG_BUILTIN = 1u << 2,
    MK_LAUNCH_FLAG_USER_SHELL = 1u << 8,
    MK_LAUNCH_FLAG_USER_DESKTOP = 1u << 9,
    MK_LAUNCH_FLAG_USER_APP = 1u << 10
};

struct mk_launch_descriptor {
    uint32_t abi_version;
    uint32_t kind;
    uint32_t service_type;
    uint32_t flags;
    uint32_t task_class;
    uint32_t stack_size;
    uint32_t argc;
    char name[MK_LAUNCH_NAME_MAX];
    char argv_data[MK_LAUNCH_ARGV_BYTES];
    void (*entry)(void);
};

struct mk_launch_context {
    uint32_t abi_version;
    uint32_t pid;
    uint32_t kind;
    uint32_t service_type;
    uint32_t flags;
    uint32_t task_class;
    uint32_t boot_flags;
    uint32_t boot_partition_lba;
    uint32_t boot_partition_sectors;
    uint32_t data_partition_lba;
    uint32_t data_partition_sectors;
    uint32_t argc;
    char name[MK_LAUNCH_NAME_MAX];
    char argv_data[MK_LAUNCH_ARGV_BYTES];
};

void mk_launch_init(void);
int mk_launch_validate_descriptor(const struct mk_launch_descriptor *descriptor);
int mk_launch_bootstrap(const struct mk_launch_descriptor *descriptor);
void mk_launch_release_pid(int pid);
const struct mk_launch_context *mk_launch_context_for_pid(int pid);
const struct mk_launch_context *mk_launch_context_current(void);

#endif
