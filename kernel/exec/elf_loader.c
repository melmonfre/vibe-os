#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>

#include <kernel/elf_loader.h>
#include <kernel/kernel.h>
#include <kernel/memory/heap.h>  /* relative path from exec/ */

/* minimal ELF definitions */
#define ELF_MAGIC 0x464C457Fu
#define PT_LOAD   1
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

/* use the same stack size as the process subsystem */
#define PROCESS_STACK_SIZE 4096

/* structures must be packed exactly as in the file */
typedef struct {
    uint32_t e_magic;
    uint8_t  e_class;
    uint8_t  e_data;
    uint8_t  e_version;
    uint8_t  e_osabi;
    uint8_t  e_abiversion;
    uint8_t  e_pad[7];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version2;
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
    if (out != NULL) {
        *out = sum;
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

process_t *elf_load(const void *elf_data, size_t size) {
    if (elf_data == NULL || size < sizeof(Elf32_Ehdr)) {
        return NULL;
    }
    const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)elf_data;
    if (ehdr->e_magic != ELF_MAGIC ||
        ehdr->e_class != ELFCLASS32 ||
        ehdr->e_data != ELFDATA2LSB ||
        ehdr->e_version != EV_CURRENT ||
        ehdr->e_version2 != EV_CURRENT) {
        return NULL;
    }
    if (!elf_osabi_supported(ehdr->e_osabi, ehdr->e_abiversion)) {
        return NULL;
    }
    if ((ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) ||
        ehdr->e_machine != EM_386 ||
        ehdr->e_phentsize != sizeof(Elf32_Phdr) ||
        ehdr->e_phnum == 0u) {
        return NULL;
    }
    if ((size_t)ehdr->e_phoff > size ||
        (size_t)ehdr->e_phnum > (size - (size_t)ehdr->e_phoff) / sizeof(Elf32_Phdr)) {
        return NULL;
    }
    /* compute span of loadable segments */
    uint32_t low = UINT32_MAX, high = 0;
    const Elf32_Phdr *phdr = (const Elf32_Phdr *)((const uint8_t *)elf_data +
                                                  ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        uint32_t seg_high;

        if (phdr[i].p_type != PT_LOAD)
            continue;
        if (phdr[i].p_memsz < phdr[i].p_filesz ||
            elf_u32_add_overflow(phdr[i].p_vaddr, phdr[i].p_memsz, &seg_high)) {
            return NULL;
        }
        if ((size_t)phdr[i].p_offset > size ||
            (size_t)phdr[i].p_filesz > size - (size_t)phdr[i].p_offset) {
            return NULL;
        }
        if (phdr[i].p_vaddr < low)
            low = phdr[i].p_vaddr;
        if (seg_high > high)
            high = seg_high;
    }
    if (low == UINT32_MAX || high <= low) {
        return NULL;
    }
    if (ehdr->e_entry < low || ehdr->e_entry >= high) {
        return NULL;
    }
    size_t total = high - low;
    uint8_t *mem = kernel_malloc(total);
    if (!mem)
        return NULL;
    memset(mem, 0, total);

    /* copy segment data into block */
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD)
            continue;
        if ((size_t)phdr[i].p_offset + phdr[i].p_filesz > size) {
            kernel_free(mem);
            return NULL;
        }
        memcpy(mem + (phdr[i].p_vaddr - low),
               (const uint8_t *)elf_data + phdr[i].p_offset,
               phdr[i].p_filesz);
        /* remaining bytes already zeroed */
    }

    /* create a process and point its entry accordingly */
    process_t *proc = kernel_malloc(sizeof(process_t));
    if (!proc) {
        kernel_free(mem);
        return NULL;
    }
    memset(proc, 0, sizeof(*proc));
    proc->pid = 0; /* will be assigned by caller / scheduler */
    proc->state = PROCESS_READY;
    proc->next = NULL;
    proc->stack = kernel_malloc(PROCESS_STACK_SIZE);
    if (!proc->stack) {
        kernel_free(mem);
        kernel_free(proc);
        return NULL;
    }
    process_setup_initial_context(proc,
                                  (uintptr_t)mem + (ehdr->e_entry - low),
                                  (uintptr_t)proc->stack + PROCESS_STACK_SIZE);
    /* note: the caller is responsible for tracking the memory block
       (mem) so that it doesn't get freed while the process runs; for our
       simple kernel we simply leak it. */
    return proc;
}
