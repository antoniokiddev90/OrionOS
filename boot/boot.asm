bits 32
section .multiboot
align 8
header_start:
dd 0xE85250D6
dd 0
dd header_end - header_start
dd -(0xE85250D6 + 0 + (header_end - header_start))
dw 0
dw 0
dd 8
header_end:
section .bss
align 16
stack_bottom:
resb 16384
stack_top:
align 8
mb_magic:
resb 4
mb_info:
resb 4
align 4096
pml4:
resb 4096
pdpt:
resb 4096
pd:
resb 4096

section .text
global _start
extern kernel_main
_start:
    cli
    mov [mb_magic], eax
    mov [mb_info], ebx
    mov esp, stack_top
    mov edi, pml4
    mov eax, pdpt
    or eax, 3
    mov [edi], eax
    mov dword [edi + 4], 0
    mov edi, pdpt
    mov eax, pd
    or eax, 3
    mov [edi], eax
    mov dword [edi + 4], 0
    mov edi, pd
    mov ecx, 512
    mov eax, 0x83
.map_pd:
    mov [edi], eax
    mov dword [edi + 4], 0
    add eax, 0x200000
    add edi, 8
    loop .map_pd
    mov eax, pml4
    mov cr3, eax
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    lgdt [gdt64.pointer]
    jmp 0x08:long_mode
bits 64
long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax
    mov rsp, stack_top
    and rsp, -16
    xor rbp, rbp
    mov edi, [mb_magic]
    mov esi, [mb_info]
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang

align 8
gdt64:
    dq 0
    dq 0x00AF9A000000FFFF
    dq 0x00AF92000000FFFF
.pointer:
    dw $ - gdt64 - 1
    dq gdt64
