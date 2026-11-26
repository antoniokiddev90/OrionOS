# OrionOS v0.07

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
- minimal C compiler 
- E1000 driver
- Net stack
- Net shell utils (ping, wget etc.)
- package manager

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
## How to use package manager
- pkg update # Update the system repositories
- pkg install <name> # installs a package 
- pkg remove <name> # removes a package
- pkg upgrade # update the packages
Note: To see all packages, go to https://orionos-repo.onrender.com/index.txt
## How to Run
```bash
make run
```
## About

OrionOS is developed by Antonio, an 11-year-old who likes cats (but unfortunately doesn't have one)

## License
MIT License
