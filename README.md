# OrionOS v0.05

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
- multi-user
- a minimal C compiler 
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
- gcc (for compiler)

## How to Build

```bash
make
make iso
```
## How to use toolchain (in OrionOS)
```bash
make run # It comes pre-installed
```

## How to use toolchain (in host)
```bash
make linux # generated toolchain in build/host/bin/
```

## How to Run
```bash
make run
```
## About

OrionOS is developed by Antonio, an 11-year-old who likes cats (but unfortunately doesn't have one)

## License
MIT License
