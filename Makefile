.RECIPEPREFIX := >
CC=clang
LD=ld.lld
NASM=nasm
CFLAGS=--target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mno-red-zone -mcmodel=kernel -Wall -Wextra -O2 -Ikernel -mno-sse -mno-sse2 -mno-mmx
USER_CFLAGS=--target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin -mno-red-zone -Wall -Wextra -O2 -Iuser/libc/include -mno-sse -mno-sse2 -mno-mmx
ASFLAGS=-f elf64
CFILES=kernel/main.c \
       kernel/x64/init.c \
       kernel/x64/gdt.c \
       kernel/x64/tss.c \
       kernel/x64/idt.c \
       kernel/x64/pic.c \
       kernel/x64/pit.c \
       kernel/x64/atkbd.c \
       kernel/x64/uart.c \
       kernel/x64/vga.c \
       kernel/log/log.c \
       kernel/log/kprintf.c \
       kernel/x64/paging.c \
       kernel/x64/pmm.c \
       kernel/x64/mb2.c \
       kernel/x64/syscall.c \
       kernel/mm/vmm.c \
       kernel/mm/heap.c \
       kernel/sched/task.c \
       kernel/block/ata.c \
       kernel/fs/ext2.c \
       kernel/fs/vfs.c \
       kernel/exec/elf.c \
       kernel/exec/process.c
ASMFILES=boot/boot.asm \
         kernel/x64/kgdt.asm \
         kernel/x64/irq.asm \
         kernel/x64/isr.asm
OBJECTS=$(CFILES:.c=.o) $(ASMFILES:.asm=.o)
USER_PROGRAMS=$(basename $(notdir $(wildcard user/util/*.c)))
USER_ELFS=$(addprefix build/user/,$(addsuffix .elf,$(USER_PROGRAMS)))
USER_COMMON=build/user/start.o build/user/libc.o
DISK_IMAGE=build/disk.img
all: kernel.elf user
%.o: %.c
>$(CC) $(CFLAGS) -c $< -o $@
%.o: %.asm
>$(NASM) $(ASFLAGS) $< -o $@
build/user:
>mkdir -p build/user
build/user/libc.o: user/libc/libc.c user/libc/include/stddef.h user/libc/include/stdint.h user/libc/include/stdio.h user/libc/include/stdlib.h user/libc/include/string.h user/libc/include/unistd.h user/libc/include/fcntl.h user/libc/include/sys/stat.h user/libc/include/syscalls.h | build/user
>$(CC) $(USER_CFLAGS) -c $< -o $@
build/user/start.o: user/libc/start.c user/libc/include/stdint.h user/libc/include/stdlib.h | build/user
>$(CC) $(USER_CFLAGS) -c $< -o $@
build/user/init.o: user/init.c user/libc/include/stdio.h user/libc/include/stdlib.h user/libc/include/unistd.h | build/user
>$(CC) $(USER_CFLAGS) -c $< -o $@
build/user/%.o: user/util/%.c user/util/common.h user/libc/include/stdio.h user/libc/include/unistd.h user/libc/include/fcntl.h | build/user
>$(CC) $(USER_CFLAGS) -Iuser/util -c $< -o $@
build/user/%.elf: build/user/%.o $(USER_COMMON) user/user.ld
>$(LD) -nostdlib -static -z max-page-size=0x1000 -T user/user.ld -o $@ $(USER_COMMON) $<
user: build/user/init.elf $(USER_ELFS)
kernel.elf: $(OBJECTS) linker.ld
>$(LD) -nostdlib -static -z max-page-size=0x1000 -T linker.ld -o $@ $(OBJECTS)
iso: kernel.elf
>mkdir -p build/iso/boot/grub
>cp kernel.elf build/iso/boot/kernel.elf
>cp boot/grub.cfg build/iso/boot/grub/grub.cfg
>grub-mkrescue -o orionos.iso build/iso
disk: user
>tools/disk.sh $(DISK_IMAGE) build/user
run: iso disk
>qemu-system-x86_64 -cdrom orionos.iso -drive file=$(DISK_IMAGE),format=raw,if=ide -m 256M -serial stdio
clean:
>rm -rf $(OBJECTS) kernel.elf orionos.iso build
.PHONY: all user iso disk clean run
