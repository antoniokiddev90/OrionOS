.RECIPEPREFIX := >
LINUX ?= 0
LINUX_CC ?= gcc
LINUX_CFLAGS ?= -std=c99 -O2 -Wall -Wextra
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
       kernel/block/gpt.c \
       kernel/fs/ext2.c \
       kernel/fs/vfs.c \
       kernel/exec/elf.c \
       kernel/exec/process.c \
       kernel/mm/user_mmap.c
ASMFILES=boot/boot.asm \
         kernel/x64/kgdt.asm \
         kernel/x64/irq.asm \
         kernel/x64/isr.asm
OBJECTS=$(CFILES:.c=.o) $(ASMFILES:.asm=.o)
USER_PROGRAMS=$(basename $(notdir $(wildcard user/util/*.c)))
USER_ELFS=$(addprefix build/user/,$(addsuffix .elf,$(USER_PROGRAMS)))
USER_COMMON=build/user/start.o build/user/libc.o build/user/mman.o build/user/malloc.o
ORION_AS_OBJECTS=$(patsubst user/orion-as/%.c,build/user/orion-as/%.o,$(wildcard user/orion-as/*.c))
ORION_LD_OBJECTS=$(patsubst user/orion-ld/%.c,build/user/orion-ld/%.o,$(wildcard user/orion-ld/*.c))
ORION_CC_OBJECTS=$(patsubst user/orion-cc/%.c,build/user/orion-cc/%.o,$(wildcard user/orion-cc/*.c))
TOOLCHAIN_ELFS=build/user/orion-as.elf build/user/orion-ld.elf build/user/orion-cc.elf
LINUX_BIN=build/host/bin
LINUX_TOOLS=$(LINUX_BIN)/orion-as $(LINUX_BIN)/orion-ld $(LINUX_BIN)/orion-cc
DISK_IMAGE=build/disk.img
ifeq ($(LINUX),1)
all: linux-toolchain
else
all: kernel.elf user
endif
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
build/user/mman.o: user/libc/mman.c user/libc/include/sys/mman.h user/libc/include/syscalls.h | build/user
>$(CC) $(USER_CFLAGS) -c $< -o $@
build/user/malloc.o: user/libc/malloc.c user/libc/include/stdlib.h user/libc/include/sys/mman.h | build/user
>$(CC) $(USER_CFLAGS) -c $< -o $@
build/user/init.o: user/init.c user/libc/include/stdio.h user/libc/include/stdlib.h user/libc/include/unistd.h | build/user
>$(CC) $(USER_CFLAGS) -c $< -o $@
build/user/%.o: user/util/%.c user/util/common.h user/libc/include/stdio.h user/libc/include/unistd.h user/libc/include/fcntl.h | build/user
>$(CC) $(USER_CFLAGS) -Iuser/util -c $< -o $@
build/user/orion-cc:
>mkdir -p build/user/orion-cc
build/user/orion-cc/%.o: user/orion-cc/%.c user/orion-cc/compiler.h | build/user/orion-cc
>$(CC) $(USER_CFLAGS) -c $< -o $@
build/user/orion-as/%.o: user/orion-as/%.c user/orion-as/as.h | build/user
>mkdir -p build/user/orion-as
>$(CC) $(USER_CFLAGS) -Iuser/orion-as -c $< -o $@
build/user/orion-ld/%.o: user/orion-ld/%.c user/orion-ld/ld.h | build/user
>mkdir -p build/user/orion-ld
>$(CC) $(USER_CFLAGS) -Iuser/orion-ld -c $< -o $@
build/user/%.elf: build/user/%.o $(USER_COMMON) user/user.ld
>$(LD) -nostdlib -static -z max-page-size=0x1000 -T user/user.ld -o $@ $(USER_COMMON) $<
build/user/orion-as.elf: $(ORION_AS_OBJECTS) $(USER_COMMON) user/user.ld
>$(LD) -nostdlib -static -z max-page-size=0x1000 -T user/user.ld -o $@ $(USER_COMMON) $(ORION_AS_OBJECTS)
build/user/orion-ld.elf: $(ORION_LD_OBJECTS) $(USER_COMMON) user/user.ld
>$(LD) -nostdlib -static -z max-page-size=0x1000 -T user/user.ld -o $@ $(USER_COMMON) $(ORION_LD_OBJECTS)
build/user/orion-cc.elf: $(ORION_CC_OBJECTS) $(USER_COMMON) user/user.ld
>$(LD) -nostdlib -static -z max-page-size=0x1000 -T user/user.ld -o $@ $(USER_COMMON) $(ORION_CC_OBJECTS)
user: build/user/init.elf $(USER_ELFS) $(TOOLCHAIN_ELFS)
$(LINUX_BIN):
>mkdir -p $(LINUX_BIN)
$(LINUX_BIN)/orion-as: $(wildcard user/orion-as/*.c) user/orion-as/as.h | $(LINUX_BIN)
>$(LINUX_CC) $(LINUX_CFLAGS) -Iuser/orion-as $(wildcard user/orion-as/*.c) -o $@
$(LINUX_BIN)/orion-ld: $(wildcard user/orion-ld/*.c) user/orion-ld/ld.h | $(LINUX_BIN)
>$(LINUX_CC) $(LINUX_CFLAGS) -Iuser/orion-ld $(wildcard user/orion-ld/*.c) -o $@
$(LINUX_BIN)/orion-cc: $(wildcard user/orion-cc/*.c) user/orion-cc/compiler.h | $(LINUX_BIN)
>$(LINUX_CC) $(LINUX_CFLAGS) $(wildcard user/orion-cc/*.c) -o $@
linux-toolchain: $(LINUX_TOOLS)
linux: linux-toolchain
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
.PHONY: all user iso disk clean run linux linux-toolchain
