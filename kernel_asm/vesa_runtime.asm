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
%define GRAPHICS_MIN_FB_ADDR 0x00100000
%define GRAPHICS_MAX_WIDTH 4096
%define GRAPHICS_MAX_HEIGHT 2160
%define A20_TEST_LOW 0x0500
%define A20_TEST_HIGH_SEG 0xFFFF
%define A20_TEST_HIGH_OFF 0x0510
%define A20_TEST_ALT_LOW 0x7DFE
%define A20_TEST_ALT_HIGH_SEG 0xFFFF
%define A20_TEST_ALT_HIGH_OFF 0x7E0E
%define A20_WAIT_LOOPS 0xFFFF

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
    sidt [saved_idtr]
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
    cld
    lidt [RM_OFF(realmode_idtr)]

    mov dx, [RM_OFF(requested_mode)]
    call vesa_set_mode_and_store_bootinfo
    mov ax, KERNEL_REALMODE_SEG
    mov ds, ax
    xor ax, ax
    mov es, ax
    jc .resume

    mov dword [RM_OFF(switch_result)], 0

.resume:
    call enable_a20
    mov ax, KERNEL_REALMODE_SEG
    mov ds, ax
    xor ax, ax
    mov es, ax
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
    push ds
    push es

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
    cmp word [es:VBE_MODE_INFO_ADDR + 0x12], GRAPHICS_MAX_WIDTH
    ja .fail
    cmp word [es:VBE_MODE_INFO_ADDR + 0x14], GRAPHICS_MAX_HEIGHT
    ja .fail
    cmp word [es:VBE_MODE_INFO_ADDR + 0x10], 0
    je .fail
    mov ax, [es:VBE_MODE_INFO_ADDR + 0x10]
    cmp ax, [es:VBE_MODE_INFO_ADDR + 0x12]
    jb .fail
    cmp dword [es:VBE_MODE_INFO_ADDR + 0x28], GRAPHICS_MIN_FB_ADDR
    jb .fail
    cmp dword [es:VBE_MODE_INFO_ADDR + 0x28], 0
    je .fail

    pop es
    pop ds
    pop di
    pop cx
    pop bx
    pop ax
    clc
    ret

.fail:
    pop es
    pop ds
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
    push ds
    push es

    call vesa_query_mode_info
    jc .fail

    mov ax, 0x4F02
    mov bx, dx
    or bx, VBE_LFB_ENABLE
    int 0x10
    cmp ax, 0x004F
    jne .fail

    mov ax, KERNEL_REALMODE_SEG
    mov ds, ax
    xor ax, ax
    mov es, ax
    call vesa_query_mode_info
    jc .fail
    call vesa_store_bootinfo_from_mode_info

    pop es
    pop ds
    pop bx
    pop ax
    clc
    ret

.fail:
    pop es
    pop ds
    pop bx
    pop ax
    stc
    ret

enable_a20:
    push ds
    push es
    push bx
    push dx
    xor ax, ax
    mov ds, ax
    mov es, ax
    call a20_is_enabled_any
    cmp ax, 1
    je .done

    mov ax, 0x2401
    int 0x15
    xor ax, ax
    mov ds, ax
    mov es, ax
    call a20_probe_settle
    call a20_is_enabled_any
    cmp ax, 1
    je .done

    in al, 0x92
    and al, 0xFE
    or al, 2
    out 0x92, al
    call a20_probe_settle
    call a20_is_enabled_any
    cmp ax, 1
    je .done

    call a20_enable_kbc
    call a20_probe_settle
    call a20_is_enabled_any
    cmp ax, 1
    je .done

    call a20_bios_reports_enabled
.done:
    pop dx
    pop bx
    pop es
    pop ds
    ret

a20_is_enabled_any:
    push bx

    call a20_is_enabled
    mov bx, ax
    call a20_is_enabled_alt
    or ax, bx
    cmp ax, 1
    je .done
    call a20_bios_reports_enabled

.done:
    pop bx
    ret

a20_probe_settle:
    push cx
    mov cx, 0x1000
.loop:
    loop .loop
    pop cx
    ret

a20_bios_reports_enabled:
    push bx

    mov ax, 0x2402
    int 0x15
    pushf
    pop bx
    xor dx, dx
    mov ds, dx
    mov es, dx
    test bx, 1
    jnz .fail
    cmp al, 1
    jne .fail
    mov ax, 1
    jmp .done

.fail:
    xor ax, ax

.done:
    pop bx
    ret

a20_is_enabled:
    pushf
    cli
    push ds
    push es
    push si
    push di

    xor ax, ax
    mov ds, ax
    mov si, A20_TEST_LOW
    mov ax, A20_TEST_HIGH_SEG
    mov es, ax
    mov di, A20_TEST_HIGH_OFF

    mov al, [ds:si]
    mov ah, [es:di]
    push ax

    mov byte [ds:si], 0x00
    mov byte [es:di], 0xFF

    cmp byte [ds:si], 0xFF

    pop ax
    mov [es:di], ah
    mov [ds:si], al

    xor ax, ax
    je .done
    mov ax, 1

.done:
    pop di
    pop si
    pop es
    pop ds
    popf
    ret

a20_is_enabled_alt:
    pushf
    cli
    push ds
    push es
    push si
    push di

    xor ax, ax
    mov ds, ax
    mov si, A20_TEST_ALT_LOW
    mov ax, A20_TEST_ALT_HIGH_SEG
    mov es, ax
    mov di, A20_TEST_ALT_HIGH_OFF

    mov al, [ds:si]
    mov ah, [es:di]
    push ax

    mov byte [ds:si], 0x55
    mov byte [es:di], 0xAA

    cmp byte [ds:si], 0xAA

    pop ax
    mov [es:di], ah
    mov [ds:si], al

    xor ax, ax
    je .done
    mov ax, 1

.done:
    pop di
    pop si
    pop es
    pop ds
    popf
    ret

a20_wait_input_empty:
    push cx
    mov cx, A20_WAIT_LOOPS
.loop:
    in al, 0x64
    test al, 0x02
    jz .ready
    loop .loop
    stc
    pop cx
    ret
.ready:
    clc
    pop cx
    ret

a20_wait_output_full:
    push cx
    mov cx, A20_WAIT_LOOPS
.loop:
    in al, 0x64
    test al, 0x01
    jnz .ready
    loop .loop
    stc
    pop cx
    ret
.ready:
    clc
    pop cx
    ret

a20_flush_output:
    push cx
    mov cx, A20_WAIT_LOOPS
.loop:
    in al, 0x64
    test al, 0x01
    jz .done
    in al, 0x60
    loop .loop
.done:
    pop cx
    ret

a20_enable_kbc_direct:
    call a20_wait_input_empty
    jc .done
    mov al, 0xD1
    out 0x64, al

    call a20_wait_input_empty
    jc .done
    mov al, 0xDF
    out 0x60, al
    call a20_wait_input_empty

.done:
    ret

a20_enable_kbc:
    call a20_wait_input_empty
    jc .done
    call a20_flush_output
    mov al, 0xAD
    out 0x64, al

    call a20_wait_input_empty
    jc .reenable
    call a20_flush_output
    mov al, 0xD0
    out 0x64, al

    call a20_wait_output_full
    jc .try_direct
    in al, 0x60
    push ax

    call a20_wait_input_empty
    jc .pop_try_direct
    mov al, 0xD1
    out 0x64, al

    call a20_wait_input_empty
    jc .pop_try_direct
    pop ax
    or al, 0x03
    out 0x60, al
    call a20_wait_input_empty
    jmp .reenable

.pop_try_direct:
    pop ax
.try_direct:
    call a20_enable_kbc_direct

.reenable:
    call a20_wait_input_empty
    mov al, 0xAE
    out 0x64, al

.done:
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
    lidt [saved_idtr]
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
saved_idtr:
    dw 0
    dd 0
realmode_idtr:
    dw 0x03FF
    dd 0x00000000
