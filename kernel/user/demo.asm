bits 64
section .text
global user_demo_start
global user_demo_end

user_demo_start:
    mov eax, 1
    mov edi, 1
    lea rsi, [rel msg]
    mov edx, msg_end - msg
    int 0x9E
.loop:
    mov eax, 2
    int 0x9E
    jmp .loop
msg: db "Hello from usermode!", 10
msg_end:
user_demo_end:
