%define KERNEL_LOAD_BASE 0x10000
%define KERNEL_REALMODE_SEG 0x1000
%define REALMODE_STACK_TOP 0xC000
%define VBE_MODE_INFO_ADDR 0x0C00
%define BOOTINFO_ADDR 0x8D00
%define BOOTINFO_FLAGS 8
%define BOOTINFO_FLAG_VESA_VALID 0x00000001
%define BOOTINFO_VESA_MODE 16
%define BOOTINFO_VESA_FB 20
%define BOOTINFO_VESA_PITCH 24
%define BOOTINFO_VESA_WIDTH 26
%define BOOTINFO_VESA_HEIGHT 28
%define BOOTINFO_VESA_BPP 30
%define VBE_LFB_ENABLE 0x4000

%define CODE_SEG 0x08
%define DATA_SEG 0x10
%define CODE16_SEG 0x18
%define DATA16_SEG 0x20
%define RM_OFF(sym) (sym - $$)
%define RM_PHYS(sym) (KERNEL_LOAD_BASE + RM_OFF(sym))

section .rmstub progbits alloc exec nowrite align=16

BITS 32
global kernel_video_bios_set_mode

kernel_video_bios_set_mode:
    mov dx, [esp + 4]
    mov [requested_mode], dx
    mov dword [switch_result], 0xFFFFFFFF
    pushad
    mov [saved_stack], esp
    mov eax, cr0
    mov [saved_cr0], eax
    cli
    lgdt [gdt_descriptor]
    jmp CODE16_SEG:pmode16_to_real

BITS 16
pmode16_to_real:
    mov ax, DATA16_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, REALMODE_STACK_TOP
    mov eax, cr0
    and eax, 0x7FFFFFFE
    mov cr0, eax
    jmp KERNEL_REALMODE_SEG:RM_OFF(realmode_apply_video_change)

realmode_apply_video_change:
    mov ax, KERNEL_REALMODE_SEG
    mov ds, ax
    mov ss, ax
    mov sp, REALMODE_STACK_TOP
    xor ax, ax
    mov es, ax

    mov dx, [RM_OFF(requested_mode)]
    call vesa_set_mode_and_store_bootinfo
    jc .resume

    mov dword [RM_OFF(switch_result)], 0

.resume:
    call enable_a20
    lgdt [cs:RM_OFF(gdt_descriptor)]
    mov eax, [RM_OFF(saved_cr0)]
    or eax, 1
    mov cr0, eax
    jmp dword CODE_SEG:RM_PHYS(pmode_video_resume)

vesa_query_mode_info:
    push ax
    push bx
    push cx
    push di

    xor ax, ax
    mov es, ax
    mov di, VBE_MODE_INFO_ADDR
    mov cx, 128
    cld
    rep stosw
    mov di, VBE_MODE_INFO_ADDR
    mov ax, 0x4F01
    mov cx, dx
    int 0x10
    cmp ax, 0x004F
    jne .fail

    xor ax, ax
    mov es, ax
    test word [es:VBE_MODE_INFO_ADDR + 0], 0x0001
    jz .fail
    test word [es:VBE_MODE_INFO_ADDR + 0], 0x0010
    jz .fail
    test word [es:VBE_MODE_INFO_ADDR + 0], 0x0080
    jz .fail
    cmp byte [es:VBE_MODE_INFO_ADDR + 0x19], 8
    jne .fail
    cmp byte [es:VBE_MODE_INFO_ADDR + 0x1B], 4
    jne .fail
    cmp word [es:VBE_MODE_INFO_ADDR + 0x12], 640
    jb .fail
    cmp word [es:VBE_MODE_INFO_ADDR + 0x14], 480
    jb .fail
    cmp word [es:VBE_MODE_INFO_ADDR + 0x10], 0
    je .fail
    cmp dword [es:VBE_MODE_INFO_ADDR + 0x28], 0
    je .fail

    pop di
    pop cx
    pop bx
    pop ax
    clc
    ret

.fail:
    pop di
    pop cx
    pop bx
    pop ax
    stc
    ret

vesa_store_bootinfo_from_mode_info:
    xor ax, ax
    mov es, ax

    mov [es:BOOTINFO_ADDR + BOOTINFO_VESA_MODE], dx
    mov eax, [es:VBE_MODE_INFO_ADDR + 0x28]
    mov [es:BOOTINFO_ADDR + BOOTINFO_VESA_FB], eax
    mov ax, [es:VBE_MODE_INFO_ADDR + 0x10]
    mov [es:BOOTINFO_ADDR + BOOTINFO_VESA_PITCH], ax
    mov ax, [es:VBE_MODE_INFO_ADDR + 0x12]
    mov [es:BOOTINFO_ADDR + BOOTINFO_VESA_WIDTH], ax
    mov ax, [es:VBE_MODE_INFO_ADDR + 0x14]
    mov [es:BOOTINFO_ADDR + BOOTINFO_VESA_HEIGHT], ax
    mov al, [es:VBE_MODE_INFO_ADDR + 0x19]
    mov [es:BOOTINFO_ADDR + BOOTINFO_VESA_BPP], al
    or dword [es:BOOTINFO_ADDR + BOOTINFO_FLAGS], BOOTINFO_FLAG_VESA_VALID
    ret

vesa_set_mode_and_store_bootinfo:
    push ax
    push bx

    call vesa_query_mode_info
    jc .fail

    mov ax, 0x4F02
    mov bx, dx
    or bx, VBE_LFB_ENABLE
    int 0x10
    cmp ax, 0x004F
    jne .fail

    call vesa_query_mode_info
    jc .fail
    call vesa_store_bootinfo_from_mode_info

    pop bx
    pop ax
    clc
    ret

.fail:
    pop bx
    pop ax
    stc
    ret

enable_a20:
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

align 8
gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
    dq 0x00009A000000FFFF
    dq 0x000092000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd RM_PHYS(gdt_start)

BITS 32
pmode_video_resume:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, [saved_stack]
    mov eax, [switch_result]
    mov [esp + 28], eax
    popad
    ret

align 4
requested_mode dw 0
align 4
saved_cr0 dd 0
saved_stack dd 0
switch_result dd 0xFFFFFFFF
