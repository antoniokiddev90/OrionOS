CC=clang
LD=ld.lld
NASM=nasm
CFLAGS=--target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -mcmodel=kernel -Wall -Wextra -O2 -Ikernel
CFLAGS += -mno-sse -mno-sse2 -mno-mmx
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
       kernel/x64/paging.c \
       kernel/x64/pmm.c \
       kernel/x64/mb2.c \
       kernel/x64/syscall.c \
       kernel/mm/vmm.c \
       kernel/mm/heap.c \
       kernel/sched/task.c \
       kernel/user/user.c
ASMFILES=boot/boot.asm \
         kernel/x64/kgdt.asm \
         kernel/x64/irq.asm \
         kernel/x64/isr.asm \
         kernel/user/demo.asm
OBJECTS=$(CFILES:.c=.o) $(ASMFILES:.asm=.o)

all: kernel.elf

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(NASM) $(ASFLAGS) $< -o $@

kernel.elf: $(OBJECTS) linker.ld
	$(LD) -nostdlib -static -z max-page-size=0x1000 -T linker.ld -o $@ $(OBJECTS)

iso: kernel.elf
	mkdir -p build/iso/boot/grub
	cp kernel.elf build/iso/boot/kernel.elf
	cp boot/grub.cfg build/iso/boot/grub/grub.cfg
	grub-mkrescue -o orionos.iso build/iso

clean:
	rm -rf $(OBJECTS) kernel.elf orionos.iso build

run: iso
	qemu-system-x86_64 -cdrom orionos.iso -m 256M -serial stdio

.PHONY: all iso clean run
