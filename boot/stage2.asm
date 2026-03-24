BITS 16
ORG 0x9000

%define KERNEL_LOAD_SEG 0x1000
%define BACKGROUND_LOAD_SEG 0x0200
%define BACKGROUND_DRAW_ADDR 0x00002000
%define BACKGROUND_DRAW_BYTES 4800
%define BACKGROUND_BUFFER_CAP 5120
%define LEGACY_VESA_ADDR 0x0500
%define SCRATCH_BUFFER 0x0600
%define BOOTINFO_ADDR 0x8000
%define BOOTINFO_FLAGS 8
%define BOOTINFO_FLAG_VESA_VALID 0x00000001
%define BOOTINFO_FLAG_MEMINFO_VALID 0x00000002
%define BOOTINFO_FLAG_BOOT_TO_DESKTOP 0x00010000
%define BOOTINFO_FLAG_BOOT_SAFE_MODE 0x00020000
%define BOOTINFO_FLAG_BOOT_RESCUE_SHELL 0x00040000
%define BOOTINFO_FLAG_BOOT_MODE_MASK (BOOTINFO_FLAG_BOOT_TO_DESKTOP | BOOTINFO_FLAG_BOOT_SAFE_MODE | BOOTINFO_FLAG_BOOT_RESCUE_SHELL)
%define BOOTINFO_VESA_MODE 16
%define BOOTINFO_VESA_FB 20
%define BOOTINFO_VESA_PITCH 24
%define BOOTINFO_VESA_WIDTH 26
%define BOOTINFO_VESA_HEIGHT 28
%define BOOTINFO_VESA_BPP 30
%define BOOTINFO_MEM_LARGEST_BASE 32
%define BOOTINFO_MEM_LARGEST_SIZE 36
%define BOOTINFO_MEM_LARGEST_END 40
%define ROOT_NAME_LEN 11

%define CODE_SEG 0x08
%define DATA_SEG 0x10

%define FAT32_END_OF_CHAIN 0x0FFFFFF8
%define MENU_ENTRY_COUNT 3
%define MENU_TIMEOUT_SECONDS 5
%define PIT_COUNTS_PER_SECOND 1193182
%define MENU_TIMEOUT_COUNTS (PIT_COUNTS_PER_SECOND * MENU_TIMEOUT_SECONDS)

stage2_entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    cld

    mov [boot_drive], dl
    call parse_bpb

    mov si, kernel_name
    call find_root_entry
    jc disk_error

    mov word [load_segment], KERNEL_LOAD_SEG
    call load_file_to_memory
    jc disk_error

    call load_optional_background
    call detect_memory
    call populate_legacy_vesa_info
    call enable_a20

    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:pmode

parse_bpb:
    mov ax, [0x7C0E]
    mov [reserved_sectors], ax
    mov al, [0x7C10]
    xor ah, ah
    mov [fat_count], ax
    mov eax, [0x7C24]
    mov [fat_size_sectors], eax
    mov eax, [0x7C2C]
    mov [root_cluster], eax
    mov eax, [0x7C1C]
    mov [hidden_sectors], eax
    mov al, [0x7C0D]
    mov [sectors_per_cluster], al

    movzx eax, word [reserved_sectors]
    add eax, [hidden_sectors]
    mov [fat_start_lba], eax

    movzx eax, word [fat_count]
    mul dword [fat_size_sectors]
    add eax, [fat_start_lba]
    mov [data_start_lba], eax
    ret

cluster_to_lba:
    ; eax = cluster
    sub eax, 2
    xor edx, edx
    mov dl, [sectors_per_cluster]
    mul edx
    add eax, [data_start_lba]
    ret

read_sector_lba:
    ; eax = lba, es:bx = buffer
    mov [dap.offset], bx
    mov [dap.segment], es
    mov [dap.lba_low], eax
    mov dword [dap.lba_high], 0
    mov word [dap.sectors], 1
    push cx
    mov cx, 3
.retry:
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jnc .ok
    mov dl, [boot_drive]
    xor ah, ah
    int 0x13
    loop .retry
    pop cx
    stc
    ret
.ok:
    pop cx
    clc
    ret

find_root_entry:
    mov [search_name_ptr], si
    mov eax, [root_cluster]
.next_cluster:
    mov [current_cluster], eax
    call cluster_to_lba
    mov [cluster_lba], eax
    xor ecx, ecx
    mov cl, [sectors_per_cluster]
    xor edi, edi
.next_sector:
    push cx
    push di
    mov eax, [cluster_lba]
    add eax, edi
    xor dx, dx
    mov es, dx
    mov bx, SCRATCH_BUFFER
    call read_sector_lba
    jc .fail

    xor si, si
.scan_entry:
    mov al, [SCRATCH_BUFFER + si]
    cmp al, 0x00
    je .not_found_popped
    cmp al, 0xE5
    je .advance
    cmp byte [SCRATCH_BUFFER + si + 11], 0x0F
    je .advance

    push si
    mov bx, [search_name_ptr]
    mov di, si
    mov cx, ROOT_NAME_LEN
.compare_name:
    mov al, [SCRATCH_BUFFER + di]
    cmp al, [bx]
    jne .name_mismatch
    inc di
    inc bx
    loop .compare_name
    pop si
    jmp .found
.name_mismatch:
    pop si

.advance:
    add si, 32
    cmp si, 512
    jb .scan_entry

    pop di
    pop cx
    inc edi
    loop .next_sector

    mov eax, [current_cluster]
    call fat_next_cluster
    jc .fail_no_pop
    cmp eax, FAT32_END_OF_CHAIN
    jae .not_found
    jmp .next_cluster

.found:
    xor eax, eax
    mov ax, [SCRATCH_BUFFER + si + 20]
    shl eax, 16
    mov ax, [SCRATCH_BUFFER + si + 26]
    mov [file_first_cluster], eax
    mov eax, [SCRATCH_BUFFER + si + 28]
    mov [file_size], eax
    pop di
    pop cx
    clc
    ret

.not_found_popped:
    pop di
    pop cx
.not_found:
    stc
    ret

.fail:
    pop di
    pop cx
.fail_no_pop:
    stc
    ret

fat_next_cluster:
    ; eax = cluster, returns eax = next cluster
    push ebx
    push ecx
    push edx

    mov ebx, eax
    shl ebx, 2
    mov eax, ebx
    shr eax, 9
    add eax, [fat_start_lba]
    mov [fat_sector_lba], eax
    and ebx, 511

    xor dx, dx
    mov es, dx
    mov bx, SCRATCH_BUFFER
    mov eax, [fat_sector_lba]
    call read_sector_lba
    jc .error

    cmp ebx, 509
    jb .single_sector

    xor dx, dx
    mov es, dx
    mov bx, SCRATCH_BUFFER + 512
    mov eax, [fat_sector_lba]
    inc eax
    call read_sector_lba
    jc .error

.single_sector:
    mov eax, [SCRATCH_BUFFER + ebx]
    and eax, 0x0FFFFFFF
    clc
    jmp .done

.error:
    stc
.done:
    pop edx
    pop ecx
    pop ebx
    ret

load_file_to_memory:
    mov eax, [file_size]
    add eax, 511
    shr eax, 9
    mov [file_remaining_sectors], eax
    mov eax, [file_first_cluster]

.cluster_loop:
    cmp dword [file_remaining_sectors], 0
    je .done
    cmp eax, 2
    jb .fail

    mov [current_cluster], eax
    call cluster_to_lba
    mov [cluster_lba], eax

    xor ecx, ecx
    mov cl, [sectors_per_cluster]
    xor edi, edi
.sector_loop:
    cmp dword [file_remaining_sectors], 0
    je .done

    push cx
    push di
    mov eax, [cluster_lba]
    add eax, edi
    push eax
    mov ax, [load_segment]
    mov es, ax
    pop eax
    xor bx, bx
    call read_sector_lba
    jc .read_fail

    add word [load_segment], 0x20
    dec dword [file_remaining_sectors]
    pop di
    pop cx
    inc edi
    loop .sector_loop

    cmp dword [file_remaining_sectors], 0
    je .done

    mov eax, [current_cluster]
    call fat_next_cluster
    jc .fail
    cmp eax, FAT32_END_OF_CHAIN
    jae .fail
    jmp .cluster_loop

.read_fail:
    pop di
    pop cx
.fail:
    stc
    ret

.done:
    clc
    ret

load_optional_background:
    mov byte [background_available], 0
    mov si, background_name
    call find_root_entry
    jc .done

    mov eax, [file_size]
    cmp eax, BACKGROUND_DRAW_BYTES
    jb .done
    cmp eax, BACKGROUND_BUFFER_CAP
    ja .done

    mov word [load_segment], BACKGROUND_LOAD_SEG
    call load_file_to_memory
    jc .done

    mov byte [background_available], 1
.done:
    clc
    ret

detect_memory:
    and dword [BOOTINFO_ADDR + BOOTINFO_FLAGS], ~BOOTINFO_FLAG_MEMINFO_VALID
    mov dword [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_BASE], 0
    mov dword [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_SIZE], 0
    mov dword [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_END], 0
    xor ebx, ebx

detect_memory_e820_loop:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    mov di, SCRATCH_BUFFER
    mov dword [SCRATCH_BUFFER + 20], 1
    int 0x15
    jc detect_memory_e820_done
    cmp eax, 0x534D4150
    jne detect_memory_e820_done
    cmp dword [SCRATCH_BUFFER + 16], 1
    jne detect_memory_e820_next
    cmp dword [SCRATCH_BUFFER + 4], 0
    jne detect_memory_e820_next
    cmp dword [SCRATCH_BUFFER + 12], 0
    jne detect_memory_e820_next

    mov eax, [SCRATCH_BUFFER + 0]
    cmp eax, 0x00100000
    jae detect_memory_e820_base_ok
    mov eax, 0x00100000
detect_memory_e820_base_ok:
    mov edx, [SCRATCH_BUFFER + 0]
    add edx, [SCRATCH_BUFFER + 8]
    jc detect_memory_e820_clamp_end
    jmp detect_memory_e820_end_ok
detect_memory_e820_clamp_end:
    mov edx, 0xFFFFF000
detect_memory_e820_end_ok:
    cmp edx, eax
    jbe detect_memory_e820_next
    mov ecx, edx
    sub ecx, eax
    cmp ecx, [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_SIZE]
    jbe detect_memory_e820_next
    mov [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_BASE], eax
    mov [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_SIZE], ecx
    mov [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_END], edx

detect_memory_e820_next:
    test ebx, ebx
    jnz detect_memory_e820_loop

detect_memory_e820_done:
    cmp dword [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_SIZE], 0
    jne detect_memory_store_bootinfo

    mov ax, 0xE801
    int 0x15
    jc detect_memory_fallback_88

    test ax, ax
    jnz detect_memory_have_low_kb
    mov ax, cx
detect_memory_have_low_kb:
    test bx, bx
    jnz detect_memory_have_high_blocks
    mov bx, dx
detect_memory_have_high_blocks:
    movzx eax, ax
    shl eax, 10
    movzx edx, bx
    shl edx, 16
    add eax, edx
    jmp detect_memory_store

detect_memory_fallback_88:
    mov ah, 0x88
    int 0x15
    jc detect_memory_done
    movzx eax, ax
    shl eax, 10

detect_memory_store:
    test eax, eax
    jz detect_memory_done
    mov dword [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_BASE], 0x00100000
    mov [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_SIZE], eax
    mov edx, eax
    add edx, 0x00100000
    mov [BOOTINFO_ADDR + BOOTINFO_MEM_LARGEST_END], edx
detect_memory_store_bootinfo:
    or dword [BOOTINFO_ADDR + BOOTINFO_FLAGS], BOOTINFO_FLAG_MEMINFO_VALID
detect_memory_done:
    ret

populate_legacy_vesa_info:
    xor ax, ax
    mov es, ax
    mov di, LEGACY_VESA_ADDR
    mov cx, 7
    rep stosw

    test dword [BOOTINFO_ADDR + BOOTINFO_FLAGS], BOOTINFO_FLAG_VESA_VALID
    jz .done

    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_MODE]
    mov [LEGACY_VESA_ADDR + 0], ax
    mov eax, [BOOTINFO_ADDR + BOOTINFO_VESA_FB]
    mov [LEGACY_VESA_ADDR + 2], eax
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_PITCH]
    mov [LEGACY_VESA_ADDR + 6], ax
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_WIDTH]
    mov [LEGACY_VESA_ADDR + 8], ax
    mov ax, [BOOTINFO_ADDR + BOOTINFO_VESA_HEIGHT]
    mov [LEGACY_VESA_ADDR + 10], ax
    mov al, [BOOTINFO_ADDR + BOOTINFO_VESA_BPP]
    mov [LEGACY_VESA_ADDR + 12], al

.done:
    ret

enable_a20:
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

disk_error:
.halt:
    hlt
    jmp .halt

align 8
gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

kernel_name db 'KERNEL  BIN'
background_name db 'VIBEBG  BIN'
boot_drive db 0
background_available db 0
reserved_sectors dw 0
fat_count dw 0
fat_size_sectors dd 0
root_cluster dd 0
hidden_sectors dd 0
fat_start_lba dd 0
data_start_lba dd 0
current_cluster dd 0
cluster_lba dd 0
fat_sector_lba dd 0
file_first_cluster dd 0
file_size dd 0
file_remaining_sectors dd 0
sectors_per_cluster db 0
load_segment dw KERNEL_LOAD_SEG
search_name_ptr dw 0

dap:
    db 16
    db 0
.sectors:
    dw 1
.offset:
    dw 0
.segment:
    dw 0
.lba_low:
    dd 0
.lba_high:
    dd 0

BITS 32
pmode:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x70000

    call vibeloader_menu
    jmp CODE_SEG:0x10000

vibeloader_menu:
    test dword [BOOTINFO_ADDR + BOOTINFO_FLAGS], BOOTINFO_FLAG_VESA_VALID
    jz .default_boot
    cmp byte [BOOTINFO_ADDR + BOOTINFO_VESA_BPP], 8
    jne .default_boot

    call set_desktop_palette
    mov dword [menu_selection], 0
    call menu_restart_timer
    mov byte [menu_dirty], 1

.loop:
    call pit_read_counter
    movzx ecx, word [menu_prev_pit]
    movzx edx, ax
    mov [menu_prev_pit], ax
    sub ecx, edx
    and ecx, 0xFFFF
    add dword [menu_elapsed_counts], ecx

    mov eax, [menu_elapsed_counts]
    cmp eax, MENU_TIMEOUT_COUNTS
    jae .boot_now

    xor edx, edx
    mov ecx, PIT_COUNTS_PER_SECOND
    div ecx
    mov ebx, MENU_TIMEOUT_SECONDS
    sub ebx, eax
    cmp ebx, [menu_seconds_left]
    je .check_keys
    mov [menu_seconds_left], ebx
    mov byte [menu_dirty], 1

.check_keys:
    call menu_poll_keyboard
    cmp eax, 2
    je .boot_now
    cmp eax, 1
    jne .maybe_draw
    mov byte [menu_dirty], 1

.maybe_draw:
    cmp byte [menu_dirty], 0
    je .loop
    call menu_render
    mov byte [menu_dirty], 0
    jmp .loop

.boot_now:
    call menu_apply_selection
    ret

.default_boot:
    mov dword [menu_selection], 0
    call menu_apply_selection
    ret

menu_restart_timer:
    call pit_read_counter
    mov [menu_prev_pit], ax
    mov dword [menu_elapsed_counts], 0
    mov dword [menu_seconds_left], MENU_TIMEOUT_SECONDS
    ret

pit_read_counter:
    xor al, al
    out 0x43, al
    in al, 0x40
    mov ah, al
    in al, 0x40
    xchg al, ah
    ret

menu_poll_keyboard:
    xor eax, eax
.poll:
    in al, 0x64
    test al, 1
    jz .done

    in al, 0x60
    cmp al, 0xE0
    jne .not_extended
    mov byte [menu_extended], 1
    jmp .poll

.not_extended:
    mov bl, [menu_extended]
    mov byte [menu_extended], 0
    test al, 0x80
    jnz .poll

    cmp bl, 0
    jne .handle_extended

    cmp al, 0x11
    je .move_up
    cmp al, 0x1F
    je .move_down
    cmp al, 0x1C
    je .confirm
    jmp .poll

.handle_extended:
    cmp al, 0x48
    je .move_up
    cmp al, 0x50
    je .move_down
    cmp al, 0x1C
    je .confirm
    jmp .poll

.move_up:
    mov eax, [menu_selection]
    test eax, eax
    jz .wrap_up
    dec eax
    jmp .store_selection
.wrap_up:
    mov eax, MENU_ENTRY_COUNT - 1
.store_selection:
    mov [menu_selection], eax
    call menu_restart_timer
    mov eax, 1
    ret

.move_down:
    mov eax, [menu_selection]
    inc eax
    cmp eax, MENU_ENTRY_COUNT
    jb .store_down
    xor eax, eax
.store_down:
    mov [menu_selection], eax
    call menu_restart_timer
    mov eax, 1
    ret

.confirm:
    mov eax, 2
    ret

.done:
    xor eax, eax
    ret

menu_apply_selection:
    mov eax, [BOOTINFO_ADDR + BOOTINFO_FLAGS]
    and eax, ~BOOTINFO_FLAG_BOOT_MODE_MASK
    cmp dword [menu_selection], 1
    je .safe_mode
    cmp dword [menu_selection], 2
    je .rescue_shell
    or eax, BOOTINFO_FLAG_BOOT_TO_DESKTOP
    jmp .store
.safe_mode:
    or eax, BOOTINFO_FLAG_BOOT_SAFE_MODE
    jmp .store
.rescue_shell:
    or eax, BOOTINFO_FLAG_BOOT_RESCUE_SHELL
.store:
    mov [BOOTINFO_ADDR + BOOTINFO_FLAGS], eax
    ret

menu_render:
    call draw_background

    mov eax, 148
    mov ebx, 104
    mov ecx, 344
    mov esi, 272
    mov dl, 1
    call draw_rect

    mov eax, 152
    mov ebx, 108
    mov ecx, 336
    mov esi, 264
    mov dl, 8
    call draw_rect

    mov eax, 152
    mov ebx, 108
    mov ecx, 336
    mov esi, 24
    mov dl, 3
    call draw_rect

    mov esi, vibeloader_title
    mov eax, 230
    mov ebx, 121
    mov ecx, 3
    mov edx, 15
    call draw_text

    mov eax, 0
    mov ebx, 188
    mov esi, menu_entry_vibeos
    call draw_menu_entry

    mov eax, 1
    mov ebx, 226
    mov esi, menu_entry_safe
    call draw_menu_entry

    mov eax, 2
    mov ebx, 264
    mov esi, menu_entry_rescue
    call draw_menu_entry

    mov eax, [menu_seconds_left]
    add al, '0'
    mov [countdown_text + 13], al

    mov esi, countdown_text
    mov eax, 224
    mov ebx, 314
    mov ecx, 2
    mov edx, 15
    call draw_text

    mov esi, menu_hint
    mov eax, 218
    mov ebx, 340
    mov ecx, 2
    mov edx, 15
    call draw_text
    ret

draw_menu_entry:
    push esi
    cmp [menu_selection], eax
    jne .normal

    push ebx
    mov eax, 198
    sub ebx, 2
    mov ecx, 244
    mov esi, 34
    mov dl, 15
    call draw_rect
    pop ebx

    mov eax, 200
    mov ecx, 240
    mov esi, 30
    mov dl, 14
    call draw_rect
    mov edx, 0
    jmp .label

.normal:
    push ebx
    mov eax, 198
    sub ebx, 2
    mov ecx, 244
    mov esi, 34
    mov dl, 15
    call draw_rect
    pop ebx

    mov eax, 200
    mov ecx, 240
    mov esi, 30
    mov dl, 7
    call draw_rect
    mov edx, 0

.label:
    pop esi
    mov eax, 220
    add ebx, 8
    mov ecx, 2
    call draw_text
    ret

draw_background:
    cmp byte [background_available], 0
    jne .image

    xor eax, eax
    xor ebx, ebx
    movzx ecx, word [BOOTINFO_ADDR + BOOTINFO_VESA_WIDTH]
    movzx esi, word [BOOTINFO_ADDR + BOOTINFO_VESA_HEIGHT]
    mov dl, 3
    call draw_rect
    ret

.image:
    xor ebp, ebp
    mov esi, BACKGROUND_DRAW_ADDR
.row_loop:
    xor edi, edi
.col_loop:
    mov dl, [esi]
    mov eax, edi
    shl eax, 3
    mov ebx, ebp
    shl ebx, 3
    mov ecx, 8
    push esi
    mov esi, 8
    call draw_rect
    pop esi
    inc esi
    inc edi
    cmp edi, 80
    jb .col_loop
    inc ebp
    cmp ebp, 60
    jb .row_loop
    ret

set_desktop_palette:
    mov dx, 0x03C8
    xor al, al
    out dx, al
    mov dx, 0x03C9

    xor ecx, ecx
.loop:
    cmp ecx, 16
    jb .ega
    mov eax, ecx
    shr eax, 5
    and eax, 0x07
    imul eax, 63
    mov ebx, 7
    xor edx, edx
    div ebx
    mov dx, 0x03C9
    out dx, al

    mov eax, ecx
    shr eax, 2
    and eax, 0x07
    imul eax, 63
    mov ebx, 7
    xor edx, edx
    div ebx
    mov dx, 0x03C9
    out dx, al

    mov eax, ecx
    and eax, 0x03
    imul eax, 63
    mov ebx, 3
    xor edx, edx
    div ebx
    mov dx, 0x03C9
    out dx, al
    inc ecx
    jmp .loop

.ega:
    movzx eax, byte [ega_palette_6bit + ecx * 3 + 0]
    out dx, al
    movzx eax, byte [ega_palette_6bit + ecx * 3 + 1]
    out dx, al
    movzx eax, byte [ega_palette_6bit + ecx * 3 + 2]
    out dx, al
    inc ecx
    cmp ecx, 256
    jb .loop
    ret

draw_rect:
    push edi
    push ebp
    mov edi, [BOOTINFO_ADDR + BOOTINFO_VESA_FB]
    mov ebp, [BOOTINFO_ADDR + BOOTINFO_VESA_PITCH]
    imul ebx, ebp
    add edi, ebx
    add edi, eax
.row:
    push ecx
    mov al, dl
    rep stosb
    pop ecx
    mov eax, ebp
    sub eax, ecx
    add edi, eax
    dec esi
    jnz .row
    pop ebp
    pop edi
    ret

draw_text:
    push ebp
    push edi
    mov edi, eax
    mov ebp, ebx
.next_char:
    lodsb
    test al, al
    jz .done
    cmp al, ' '
    je .advance
    push esi
    push ecx
    push edx
    push edi
    push ebp
    mov ebx, edi
    mov edi, ebp
    call draw_glyph
    pop ebp
    pop edi
    pop edx
    pop ecx
    pop esi
.advance:
    mov eax, ecx
    imul eax, 6
    add edi, eax
    jmp .next_char
.done:
    pop edi
    pop ebp
    ret

draw_glyph:
    push ebp
    push ebx
    push edi
    push ecx
    push edx
    push esi
    call glyph_ptr_from_char
    test esi, esi
    jz .done
    mov [glyph_ptr_tmp], esi
    xor ebp, ebp
.row_loop:
    cmp ebp, 7
    jae .done
    mov esi, [glyph_ptr_tmp]
    mov al, [esi + ebp]
    mov [glyph_row_bits], al
    xor edi, edi
.col_loop:
    cmp edi, 5
    jae .next_row
    mov al, [glyph_row_bits]
    mov bl, [glyph_masks + edi]
    test al, bl
    jz .skip_pixel

    mov eax, [esp + 16]
    mov ecx, [esp + 8]
    mov edx, edi
    imul edx, ecx
    add eax, edx

    mov ebx, [esp + 12]
    mov edx, ebp
    imul edx, ecx
    add ebx, edx

    mov esi, ecx
    mov edx, [esp + 4]
    call draw_rect

.skip_pixel:
    inc edi
    jmp .col_loop

.next_row:
    inc ebp
    jmp .row_loop

.done:
    pop esi
    pop edx
    pop ecx
    pop edi
    pop ebx
    pop ebp
    ret

glyph_ptr_from_char:
    cmp al, 'A'
    jb .digit_or_space
    cmp al, 'Z'
    ja .digit_or_space
    movzx eax, al
    sub eax, 'A'
    imul eax, 7
    lea esi, [font_glyphs + eax]
    ret

.digit_or_space:
    cmp al, '0'
    jb .space
    cmp al, '9'
    ja .space
    movzx eax, al
    sub eax, '0'
    add eax, 26
    imul eax, 7
    lea esi, [font_glyphs + eax]
    ret

.space:
    mov esi, font_space
    ret

menu_selection dd 0
menu_elapsed_counts dd 0
menu_seconds_left dd MENU_TIMEOUT_SECONDS
menu_prev_pit dw 0
menu_dirty db 0
menu_extended db 0
glyph_row_bits db 0
glyph_ptr_tmp dd 0

vibeloader_title db 'VIBELOADER', 0
menu_entry_vibeos db 'VIBEOS', 0
menu_entry_safe db 'SAFE MODE', 0
menu_entry_rescue db 'RESCUE SHELL', 0
countdown_text db 'AUTO BOOT IN 0S', 0
menu_hint db 'ARROWS OR ENTER', 0

glyph_masks db 16, 8, 4, 2, 1

ega_palette_6bit:
    db 0, 0, 0
    db 0, 0, 42
    db 0, 42, 0
    db 0, 42, 42
    db 42, 0, 0
    db 42, 0, 42
    db 42, 21, 0
    db 42, 42, 42
    db 21, 21, 21
    db 21, 21, 63
    db 21, 63, 21
    db 21, 63, 63
    db 63, 21, 21
    db 63, 21, 63
    db 63, 63, 21
    db 63, 63, 63

font_glyphs:
    ; A-Z
    db 0x0E,0x11,0x11,0x1F,0x11,0x11,0x11
    db 0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E
    db 0x0E,0x11,0x10,0x10,0x10,0x11,0x0E
    db 0x1E,0x11,0x11,0x11,0x11,0x11,0x1E
    db 0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F
    db 0x1F,0x10,0x10,0x1E,0x10,0x10,0x10
    db 0x0E,0x11,0x10,0x10,0x13,0x11,0x0F
    db 0x11,0x11,0x11,0x1F,0x11,0x11,0x11
    db 0x0E,0x04,0x04,0x04,0x04,0x04,0x0E
    db 0x01,0x01,0x01,0x01,0x11,0x11,0x0E
    db 0x11,0x12,0x14,0x18,0x14,0x12,0x11
    db 0x10,0x10,0x10,0x10,0x10,0x10,0x1F
    db 0x11,0x1B,0x15,0x15,0x11,0x11,0x11
    db 0x11,0x19,0x15,0x13,0x11,0x11,0x11
    db 0x0E,0x11,0x11,0x11,0x11,0x11,0x0E
    db 0x1E,0x11,0x11,0x1E,0x10,0x10,0x10
    db 0x0E,0x11,0x11,0x11,0x15,0x12,0x0D
    db 0x1E,0x11,0x11,0x1E,0x14,0x12,0x11
    db 0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E
    db 0x1F,0x04,0x04,0x04,0x04,0x04,0x04
    db 0x11,0x11,0x11,0x11,0x11,0x11,0x0E
    db 0x11,0x11,0x11,0x11,0x11,0x0A,0x04
    db 0x11,0x11,0x11,0x15,0x15,0x15,0x0A
    db 0x11,0x11,0x0A,0x04,0x0A,0x11,0x11
    db 0x11,0x11,0x0A,0x04,0x04,0x04,0x04
    db 0x1F,0x01,0x02,0x04,0x08,0x10,0x1F
    ; 0-9
    db 0x0E,0x11,0x13,0x15,0x19,0x11,0x0E
    db 0x04,0x0C,0x04,0x04,0x04,0x04,0x0E
    db 0x0E,0x11,0x01,0x02,0x04,0x08,0x1F
    db 0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E
    db 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02
    db 0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E
    db 0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E
    db 0x1F,0x01,0x02,0x04,0x08,0x08,0x08
    db 0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E
    db 0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E
font_space:
    db 0x00,0x00,0x00,0x00,0x00,0x00,0x00
