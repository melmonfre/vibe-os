BITS 16
%ifndef BOOT_LOAD_ADDR
%define BOOT_LOAD_ADDR 0x7C00
%endif
ORG BOOT_LOAD_ADDR

%define KERNEL_SEG 0x1000
%define KERNEL_OFF 0x0000
%define BOOTINFO_ADDR 0x8200
%define BOOTINFO_MAGIC 0x56424D49
%define E820_BUF 0x8220
%define VESA_INFO_ADDR 0x0500
%define VESA_CTRLINFO_ADDR 0x8400
%define VESA_MODEINFO_ADDR 0x8600
%define VESA_MODE_LIST_MAX 5
%define MIN_USABLE_ADDR 0x00100000
; Number of 512-byte sectors to read for the kernel image.
; Keep enough room for the monolithic userland while preserving the disk layout.
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 1280
%endif
%ifndef KERNEL_START_LBA
%define KERNEL_START_LBA 1
%endif

%define CODE_SEG 0x08
%define DATA_SEG 0x10

start:

    cli
    xor ax,ax
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov sp,0x7C00
    ; keep interrupts disabled until IDT is set by the kernel

    mov [boot_drive],dl

    call try_enable_best_vesa
    call load_kernel
    jc disk_error

    call detect_memory

    call enable_a20

    lgdt [gdt_descriptor]

    cli                 ; ensure IF=0 before entering protected mode
    mov eax,cr0
    or eax,1
    mov cr0,eax

    jmp CODE_SEG:pmode

; -----------------------------
; kernel loader
; -----------------------------

load_kernel:

    pusha
    call disk_reset
    call load_kernel_lba
    jnc .done
    call disk_reset
    call load_kernel_chs

.done:

    popa
    ret

disk_reset:

    mov dl,[boot_drive]
    xor ah,ah
    int 0x13
    ret

load_kernel_chs:

    mov dl,[boot_drive]
    mov ah,0x08
    int 0x13
    jc .fail

    xor ax,ax
    mov al,cl
    and ax,0x003F
    jz .fail
    mov [chs_sectors_per_track],ax

    xor ax,ax
    mov al,dh
    inc ax
    jz .fail
    mov [chs_heads_count],ax

    mov ax,KERNEL_SEG
    mov es,ax
    mov dword [chs_lba],KERNEL_START_LBA
    mov word [chs_remaining],KERNEL_SECTORS

.next:
    mov eax,[chs_lba]
    xor edx,edx
    xor ecx,ecx
    mov cx,[chs_sectors_per_track]
    div ecx
    inc dl
    mov [chs_sector],dl

    xor edx,edx
    xor ecx,ecx
    mov cx,[chs_heads_count]
    div ecx
    cmp eax,1023
    ja .fail

    mov [chs_head],dl
    mov ch,al
    mov cl,[chs_sector]
    mov bl,ah
    and bl,0x03
    shl bl,6
    or cl,bl
    mov dh,[chs_head]
    mov dl,[boot_drive]
    mov ah,0x02
    mov al,0x01
    xor bx,bx
    int 0x13
    jc .fail

    mov ax,es
    add ax,0x20
    mov es,ax
    inc dword [chs_lba]
    dec word [chs_remaining]
    jnz .next

    clc
    ret

.fail:
    stc
    ret

try_enable_best_vesa:
    mov byte [VESA_INFO_ADDR + 13],0
    mov word [best_vesa_mode],0xFFFF
    mov word [best_vesa_area + 0],0
    mov word [best_vesa_area + 2],0

    mov dword [VESA_CTRLINFO_ADDR],'2EBV'
    mov ax,0x4F00
    mov di,VESA_CTRLINFO_ADDR
    int 0x10
    cmp ax,0x004F
    jne .done
    cmp dword [VESA_CTRLINFO_ADDR], 'ASEV'
    jne .done

    les si,[VESA_CTRLINFO_ADDR + 14]

.next_mode:
    mov bx,[es:si]
    cmp bx,0xFFFF
    je .apply_best
    add si,2

    push si
    mov ax,0x4F01
    mov cx,bx
    mov di,VESA_MODEINFO_ADDR
    int 0x10
    pop si
    cmp ax,0x004F
    jne .next_mode

    mov ax,[VESA_MODEINFO_ADDR + 0]
    and ax,0x0091
    cmp ax,0x0091
    jne .next_mode
    cmp byte [VESA_MODEINFO_ADDR + 25],8
    jne .next_mode
    cmp byte [VESA_MODEINFO_ADDR + 27],4
    jne .next_mode

    call remember_vesa_mode
    mov ax,[VESA_MODEINFO_ADDR + 18]
    mul word [VESA_MODEINFO_ADDR + 20]
    cmp dx,[best_vesa_area + 2]
    jb .next_mode
    ja .record_best
    cmp ax,[best_vesa_area + 0]
    jbe .next_mode

.record_best:
    mov [best_vesa_area + 0],ax
    mov [best_vesa_area + 2],dx
    mov [best_vesa_mode],bx
    jmp .next_mode

.apply_best:
    cmp word [best_vesa_mode],0xFFFF
    je .fallback_640

    mov ax,0x4F01
    mov cx,[best_vesa_mode]
    mov di,VESA_MODEINFO_ADDR
    int 0x10
    cmp ax,0x004F
    jne .done

    mov ax,0x4F02
    mov bx,[best_vesa_mode]
    or bx,0x4000
    int 0x10
    cmp ax,0x004F
    jne .done

    mov ax,[best_vesa_mode]
    call store_current_vesa_mode
    jmp .done

.fallback_640:
    cmp word [VESA_INFO_ADDR + 0],0
    jne .done
    mov ax,0x4F01
    mov cx,0x0101
    mov di,VESA_MODEINFO_ADDR
    int 0x10
    cmp ax,0x004F
    jne .done
    mov ax,0x4F02
    mov bx,0x4101
    int 0x10
    cmp ax,0x004F
    jne .done
    mov ax,0x0101
    call store_current_vesa_mode

.done:
    ret

store_current_vesa_mode:
    mov [VESA_INFO_ADDR + 0],ax
    mov eax,[VESA_MODEINFO_ADDR + 40]
    mov [VESA_INFO_ADDR + 2],eax
    mov ax,[VESA_MODEINFO_ADDR + 16]
    mov [VESA_INFO_ADDR + 6],ax
    mov ax,[VESA_MODEINFO_ADDR + 18]
    mov [VESA_INFO_ADDR + 8],ax
    mov ax,[VESA_MODEINFO_ADDR + 20]
    mov [VESA_INFO_ADDR + 10],ax
    mov al,[VESA_MODEINFO_ADDR + 25]
    mov [VESA_INFO_ADDR + 12],al
    ret

remember_vesa_mode:
    movzx cx,byte [VESA_INFO_ADDR + 13]
    cmp cx,VESA_MODE_LIST_MAX
    jae .done

    shl cx,2
    mov bx,cx
    mov ax,[VESA_MODEINFO_ADDR + 18]
    mov [VESA_INFO_ADDR + 14 + bx],ax
    mov ax,[VESA_MODEINFO_ADDR + 20]
    mov [VESA_INFO_ADDR + 16 + bx],ax
    inc byte [VESA_INFO_ADDR + 13]

.done:
    ret

load_kernel_lba:

    mov dl,[boot_drive]
    mov ah,0x41
    mov bx,0x55AA
    int 0x13
    jc .fail
    cmp bx,0xAA55
    jne .fail
    test cx,1
    jz .fail

    mov ax,KERNEL_SEG
    mov [dap_segment],ax
    mov word [dap_offset],0
    mov dword [dap_lba_low],KERNEL_START_LBA
    mov dword [dap_lba_high],0
    mov word [dap_remaining],KERNEL_SECTORS

.next:
    mov dl,[boot_drive]
    mov si,dap_packet
    mov ah,0x42
    int 0x13
    jc .fail

    mov ax,[dap_segment]
    add ax,0x20
    mov [dap_segment],ax
    inc dword [dap_lba_low]
    dec word [dap_remaining]
    jnz .next

    clc
    ret

.fail:
    stc
    ret

; -----------------------------
; memory map
; -----------------------------

detect_memory:

    mov dword [BOOTINFO_ADDR + 0], BOOTINFO_MAGIC
    mov dword [BOOTINFO_ADDR + 4], 0x00500000
    mov dword [BOOTINFO_ADDR + 8], 0x00400000
    mov dword [BOOTINFO_ADDR + 12], 0x00900000

    xor ax,ax
    mov es,ax
    xor ebx,ebx

.next:
    mov eax,0xE820
    mov edx,0x534D4150
    mov ecx,20
    mov di,E820_BUF
    int 0x15
    jc .done
    cmp eax,0x534D4150
    jne .done
    cmp dword [E820_BUF + 16],1
    jne .cont
    cmp dword [E820_BUF + 4],0
    jne .cont
    cmp dword [E820_BUF + 12],0
    jne .cont

    mov eax,[E820_BUF + 0]
    mov edx,[E820_BUF + 8]
    test edx,edx
    jz .cont
    mov ecx,eax
    add ecx,edx
    jc .cont
    cmp ecx,MIN_USABLE_ADDR
    jbe .cont
    cmp eax,MIN_USABLE_ADDR
    jae .size_ready
    mov eax,MIN_USABLE_ADDR
    mov edx,ecx
    sub edx,eax
    jz .cont

.size_ready:
    cmp edx,[BOOTINFO_ADDR + 8]
    jbe .cont
    mov [BOOTINFO_ADDR + 4],eax
    mov [BOOTINFO_ADDR + 8],edx
    mov [BOOTINFO_ADDR + 12],ecx

.cont:
    test ebx,ebx
    jne .next

.done:
    ret

; -----------------------------
; A20
; -----------------------------

enable_a20:
    mov ax,0x2401
    int 0x15

    in al,0x92
    or al,2
    out 0x92,al

    call enable_a20_kbc
    ret

enable_a20_kbc:
    call ps2_wait_write
    mov al,0xAD
    out 0x64,al

    call ps2_wait_write
    mov al,0xD0
    out 0x64,al

    call ps2_wait_read
    in al,0x60
    push ax

    call ps2_wait_write
    mov al,0xD1
    out 0x64,al

    call ps2_wait_write
    pop ax
    or al,2
    out 0x60,al

    call ps2_wait_write
    mov al,0xAE
    out 0x64,al
    ret

ps2_wait_write:
    in al,0x64
    test al,2
    jz .done
    jmp ps2_wait_write

.done:
    ret

ps2_wait_read:
    in al,0x64
    test al,1
    jnz .done
    jmp ps2_wait_read

.done:
    ret

; -----------------------------
; error
; -----------------------------

disk_error:
    mov ah,0x0E
    mov al,'S'
    int 0x10
.halt:
    hlt
    jmp .halt

; -----------------------------
; gdt
; -----------------------------

align 8
gdt_start:

dq 0
dq 0x00CF9A000000FFFF
dq 0x00CF92000000FFFF

gdt_end:

gdt_descriptor:
dw gdt_end-gdt_start-1
dd gdt_start

; -----------------------------
; data
; -----------------------------

boot_drive db 0
chs_sectors_per_track dw 0
chs_heads_count dw 0
chs_lba dd 0
chs_remaining dw 0
chs_head db 0
chs_sector db 0
best_vesa_mode dw 0
best_vesa_area dd 0
dap_packet:
db 0x10
db 0
dw 1
dap_offset dw 0
dap_segment dw KERNEL_SEG
dap_lba_low dd KERNEL_START_LBA
dap_lba_high dd 0
dap_remaining dw 0

; -----------------------------
; protected mode
; -----------------------------

BITS 32

pmode:

    cli
    mov ax,DATA_SEG
    mov ds,ax
    mov es,ax
    mov fs,ax
    mov gs,ax
    mov ss,ax

    mov esp,0x70000

    ; jump to 32-bit kernel entry at physical 0x10000 (identity mapped)
    jmp CODE_SEG:0x10000

times 1022-($-$$) db 0
dw 0xAA55
