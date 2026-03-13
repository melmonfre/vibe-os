; simple cooperative context switch routine
; arguments (cdecl):
;   [esp]   return address
;   [esp+4] pointer to old task (regs block) or NULL
;   [esp+8] pointer to new task (regs block)
;
; the layout of the regs block is defined in kernel/include/process.h.
; because regs_t is the first member of process_t the two pointers
; are interchangeable as long as we keep the struct order.

    global context_switch
context_switch:
    ; grab parameters
    mov ebx, [esp+4]    ; old regs pointer (may be NULL)
    mov esi, [esp+8]    ; new regs pointer

    cmp ebx, 0
    je .load_new        ; nothing to save if old==NULL

    ; save caller register state into *ebx
    pusha                      ; saves eax..edi (esp value before pusha)
    mov eax, [esp + 32]        ; return address pushed by CALL
    mov [ebx + 0], eax         ; regs.eip
    mov eax, [esp + 16]        ; original ESP value
    mov [ebx + 4], eax         ; regs.esp
    mov eax, [esp + 20]
    mov [ebx + 8], eax         ; regs.ebp
    mov eax, [esp + 0]
    mov [ebx +12], eax         ; regs.eax
    mov eax, [esp + 12]
    mov [ebx +16], eax         ; regs.ebx
    mov eax, [esp + 4]
    mov [ebx +20], eax         ; regs.ecx
    mov eax, [esp + 8]
    mov [ebx +24], eax         ; regs.edx
    mov eax, [esp + 24]
    mov [ebx +28], eax         ; regs.esi
    mov eax, [esp + 28]
    mov [ebx +32], eax         ; regs.edi
    popa                        ; restore caller registers

.load_new:
    ; load new context pointed to by esi
    mov ebx, esi        ; ebx := new regs
    mov eax, [ebx +12]  ; eax
    mov ecx, [ebx +20]  ; ecx
    mov edx, [ebx +24]  ; edx
    mov ebp, [ebx + 8]  ; ebp
    mov esi, [ebx +28]  ; esi
    mov edi, [ebx +32]  ; edi
    mov esp, [ebx + 4]  ; esp
    jmp [ebx + 0]       ; jump to new eip
