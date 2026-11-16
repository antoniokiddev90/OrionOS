# OrionOS v0.04

OrionOS is a minimal operating system written in C and NASM.

## Features

- GDT
- IDT
- PIC
- PIT
- PS/2 Keyboard
- PMM
- VMM
- Heap
- Scheduler
- Ring 3
- EXT2 (read and write)
- libc
- coreutils
- userland shell
- /init

## Architecture

- Target: x86 (64-bit long mode)
- Language: C (freestanding) and NASM

## Requirements

- nasm
- clang
- llvm
- ld.lld
- qemu-system-x86_64
- xorriso
- grub-mkrescue
- mke2fs
- debugfs

## How to Build

```bash
make
make iso
```

## How to Run
```bash
make run
```
## About

OrionOS is developed by Antonio, an 11-year-old who likes cats (but unfortunately doesn't have one)

## License
MIT License

