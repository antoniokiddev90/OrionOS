bits 64
section .text
global gdt_load
gdt_load:
    lgdt [rdi]
    push 0x08
    lea rax, [rel .reload]
    push rax
    retfq
.reload:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    ret
