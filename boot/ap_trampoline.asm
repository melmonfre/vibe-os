BITS 16
ORG 0

%define AP_DEBUG_STAGE_ADDR 0x6FF0

start:
    cli
    cld
    mov byte [AP_DEBUG_STAGE_ADDR], 0x11
    xor esi, esi
    mov si, cs
    shl esi, 4
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    lgdt [cs:gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    mov byte [AP_DEBUG_STAGE_ADDR], 0x12
    jmp far dword [cs:protected_mode_ptr]

BITS 32

protected_mode:
    mov byte [AP_DEBUG_STAGE_ADDR], 0x21
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov eax, [esi + page_dir_ptr]
    test eax, eax
    jz .paging_ready
    mov cr3, eax
    mov eax, cr4
    or eax, 0x10
    mov cr4, eax
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    jmp short .paging_ready
.paging_ready:
    mov esp, [esi + stack_ptr]
    mov eax, [esi + entry_ptr]
    mov byte [AP_DEBUG_STAGE_ADDR], 0x22
    jmp eax

align 8, db 0
gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd 0

protected_mode_ptr:
    dd 0
    dw 0x08

entry_ptr:
    dd 0

stack_ptr:
    dd 0

page_dir_ptr:
    dd 0

end:
