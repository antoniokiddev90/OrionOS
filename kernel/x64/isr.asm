bits 64
section .text

extern exception_dispatch
extern syscall_dispatch
extern sched_should_switch
extern schedule

%macro EXC_NOERR 1
global exc%1
exc%1:
    push 0
    push %1
    jmp exc_common
%endmacro

%macro EXC_ERR 1
global exc%1
exc%1:
    push %1
    jmp exc_common
%endmacro

exc_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, rsp
    call exception_dispatch
.hang:
    cli
    hlt
    jmp .hang

EXC_NOERR 0
EXC_NOERR 6
EXC_ERR   8
EXC_ERR   13
EXC_ERR   14

global syscall_entry
syscall_entry:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, [rsp + 112]
    mov rsi, [rsp + 72]
    mov rdx, [rsp + 80]
    mov rcx, [rsp + 88]
    mov r8,  [rsp + 40]
    mov r9,  [rsp + 56]

    call syscall_dispatch
    mov [rsp + 112], rax

    call sched_should_switch
    test rax, rax
    jz .restore

    mov rdi, rsp
    call schedule
    mov rsp, rax

.restore:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq
