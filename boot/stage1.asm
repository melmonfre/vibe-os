BITS 16
ORG 0x7C00

%define KERNEL_SEG 0x1000
%define KERNEL_OFF 0x0000
; Number of 512-byte sectors to read for the kernel image.
; The current kernel.bin is ~170 KiB (~332 sectors) after embedding
; userland, Lua, SectorC and the desktop apps. Load 384 sectors for headroom.
%define KERNEL_SECTORS 384

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

    mov ax,0x0003
    int 0x10

    mov si,msg_boot
    call print

    call load_kernel
    jc disk_error

    mov si,msg_loaded
    call print

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

    mov word [disk_address_packet.sectors],1
    mov word [disk_address_packet.offset],0
    mov word [disk_address_packet.segment],KERNEL_SEG
    mov dword [disk_address_packet.lba_low],1
    mov dword [disk_address_packet.lba_high],0
    mov cx,KERNEL_SECTORS

.next:
    mov si,disk_address_packet
    mov dl,[boot_drive]
    mov ah,0x42
    int 0x13
    jc .fail

    add word [disk_address_packet.segment],0x20
    inc dword [disk_address_packet.lba_low]
    adc dword [disk_address_packet.lba_high],0
    loop .next

    popa
    clc
    ret

.fail:

    popa
    stc
    ret

; -----------------------------
; A20
; -----------------------------

enable_a20:

    in al,0x92
    or al,2
    out 0x92,al
    ret

; -----------------------------
; print
; -----------------------------

print:

.next:
    lodsb
    test al,al
    jz .done

    mov ah,0x0E
    mov bx,0x0007
    int 0x10

    jmp .next

.done:
    ret

; -----------------------------
; error
; -----------------------------

disk_error:

    mov si,msg_error
    call print

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

disk_address_packet:
    db 16
    db 0
.sectors:
    dw 1
.offset:
    dw 0
.segment:
    dw KERNEL_SEG
.lba_low:
    dd 1
.lba_high:
    dd 0

msg_boot db "bootloader start",13,10,0
msg_loaded db "kernel loaded",13,10,0
msg_error db "disk error",13,10,0

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

times 510-($-$$) db 0
dw 0xAA55
