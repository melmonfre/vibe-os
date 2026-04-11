#include <stdint.h>
#include <stddef.h>

#include <kernel/elf_loader.h>
#include <kernel/kernel.h>
#include <kernel/kernel_string.h>
#include <kernel/memory/heap.h>

#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define EI_OSABI 7
#define EI_ABIVERSION 8
#define EI_NIDENT 16

#define ELFMAG0 0x7fu
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define PT_NULL 0u
#define PT_LOAD 1u
#define PT_DYNAMIC 2u
#define PT_INTERP 3u
#define PT_NOTE 4u
#define PT_PHDR 6u
#define PT_TLS 7u
#define PT_GNU_STACK 0x6474e551u

#define ELFCLASS32 1u
#define ELFDATA2LSB 1u
#define EV_CURRENT 1u
#define ELFOSABI_NONE 0u
#define ELFOSABI_GNU 3u
#define ELFOSABI_FREEBSD 9u
#define ELFOSABI_OPENBSD 12u
#define ET_EXEC 2u
#define ET_DYN 3u
#define EM_386 3u
#define PN_XNUM 0xffffu

#define PROCESS_STACK_SIZE 4096u

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

static int elf_u32_add_overflow(uint32_t a, uint32_t b, uint32_t *out) {
    uint32_t sum = a + b;

    if (sum < a) {
        return 1;
    }
    if (out != 0) {
        *out = sum;
    }
    return 0;
}

static int elf_u32_sub_underflow(uint32_t a, uint32_t b, uint32_t *out) {
    if (a < b) {
        return 1;
    }
    if (out != 0) {
        *out = a - b;
    }
    return 0;
}

static int elf_osabi_supported(uint8_t osabi, uint8_t abiversion) {
    switch (osabi) {
    case ELFOSABI_NONE:
    case ELFOSABI_GNU:
        return abiversion == 0u;
    case ELFOSABI_FREEBSD:
    case ELFOSABI_OPENBSD:
        return abiversion <= 1u;
    default:
        return 0;
    }
}

static int elf_ident_supported(const unsigned char ident[EI_NIDENT]) {
    if (ident == 0) {
        return 0;
    }
    if (ident[EI_MAG0] != ELFMAG0 ||
        ident[EI_MAG1] != ELFMAG1 ||
        ident[EI_MAG2] != ELFMAG2 ||
        ident[EI_MAG3] != ELFMAG3) {
        return 0;
    }
    if (ident[EI_CLASS] != ELFCLASS32 ||
        ident[EI_DATA] != ELFDATA2LSB ||
        ident[EI_VERSION] != EV_CURRENT) {
        return 0;
    }
    return elf_osabi_supported(ident[EI_OSABI], ident[EI_ABIVERSION]);
}

static int elf_align_valid(uint32_t align) {
    if (align == 0u || align == 1u) {
        return 1;
    }
    return (align & (align - 1u)) == 0u;
}

process_t *elf_load(const void *elf_data, size_t size) {
    const Elf32_Ehdr *ehdr;
    const Elf32_Phdr *phdr;
    uint32_t low = UINT32_MAX;
    uint32_t high = 0u;
    uint32_t load_count = 0u;
    uint8_t *mem;
    process_t *proc;
    uintptr_t entry_point;
    size_t total;
    uint16_t i;

    if (elf_data == 0 || size < sizeof(Elf32_Ehdr)) {
        return 0;
    }

    ehdr = (const Elf32_Ehdr *)elf_data;
    if (!elf_ident_supported(ehdr->e_ident)) {
        return 0;
    }
    if (ehdr->e_ehsize != sizeof(Elf32_Ehdr) ||
        ehdr->e_version != EV_CURRENT ||
        (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) ||
        ehdr->e_machine != EM_386 ||
        ehdr->e_phentsize != sizeof(Elf32_Phdr) ||
        ehdr->e_phnum == 0u ||
        ehdr->e_phnum == PN_XNUM) {
        return 0;
    }
    if ((size_t)ehdr->e_phoff > size ||
        (size_t)ehdr->e_phnum > (size - (size_t)ehdr->e_phoff) / sizeof(Elf32_Phdr)) {
        return 0;
    }

    phdr = (const Elf32_Phdr *)((const uint8_t *)elf_data + ehdr->e_phoff);
    for (i = 0; i < ehdr->e_phnum; ++i) {
        uint32_t seg_high;
        uint32_t align_mask;
        uint32_t offset_delta;

        if ((size_t)phdr[i].p_offset > size ||
            (size_t)phdr[i].p_filesz > size - (size_t)phdr[i].p_offset) {
            return 0;
        }

        switch (phdr[i].p_type) {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_GNU_STACK:
            continue;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_TLS:
            return 0;
        case PT_LOAD:
            break;
        default:
            continue;
        }

        if (phdr[i].p_memsz == 0u) {
            continue;
        }
        if (phdr[i].p_memsz < phdr[i].p_filesz ||
            !elf_align_valid(phdr[i].p_align) ||
            elf_u32_add_overflow(phdr[i].p_vaddr, phdr[i].p_memsz, &seg_high)) {
            return 0;
        }
        if (phdr[i].p_align > 1u) {
            align_mask = phdr[i].p_align - 1u;
            if (elf_u32_sub_underflow(phdr[i].p_vaddr, phdr[i].p_offset, &offset_delta) ||
                (offset_delta & align_mask) != 0u) {
                return 0;
            }
        }

        if (phdr[i].p_vaddr < low) {
            low = phdr[i].p_vaddr;
        }
        if (seg_high > high) {
            high = seg_high;
        }
        load_count += 1u;
    }

    if (load_count == 0u || low == UINT32_MAX || high <= low) {
        return 0;
    }
    if (ehdr->e_entry < low || ehdr->e_entry >= high) {
        return 0;
    }

    total = (size_t)(high - low);
    mem = kernel_malloc(total);
    if (mem == 0) {
        return 0;
    }
    memset(mem, 0, total);

    for (i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type != PT_LOAD || phdr[i].p_memsz == 0u) {
            continue;
        }
        memcpy(mem + (size_t)(phdr[i].p_vaddr - low),
               (const uint8_t *)elf_data + phdr[i].p_offset,
               phdr[i].p_filesz);
    }

    proc = kernel_malloc(sizeof(*proc));
    if (proc == 0) {
        kernel_free(mem);
        return 0;
    }
    memset(proc, 0, sizeof(*proc));

    proc->pid = 0;
    proc->state = PROCESS_READY;
    proc->kind = PROCESS_KIND_USER;
    proc->current_cpu = -1;
    proc->preferred_cpu = -1;
    proc->last_cpu = -1;
    proc->stack_size = PROCESS_STACK_SIZE;
    proc->stack = kernel_malloc(PROCESS_STACK_SIZE);
    if (proc->stack == 0) {
        kernel_free(mem);
        kernel_free(proc);
        return 0;
    }

    entry_point = (uintptr_t)mem + (uintptr_t)(ehdr->e_entry - low);
    process_setup_initial_context(proc,
                                  entry_point,
                                  (uintptr_t)proc->stack + PROCESS_STACK_SIZE);
    process_set_abi_metadata(proc,
                             PROCESS_ABI_ELF32,
                             ehdr->e_ident[EI_ABIVERSION],
                             ehdr->e_ident[EI_OSABI],
                             ehdr->e_machine,
                             (uintptr_t)mem,
                             (uint32_t)total,
                             entry_point);

    return proc;
}
