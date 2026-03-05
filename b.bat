@ECHO OFF

PUSHD bin

i686-elf-as ../src/boot.s -o boot.o
i686-elf-gcc -c ../src/kernel.c -o kernel.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -T ../linker.ld -o VOS -ffreestanding -O2 -nostdlib boot.o kernel.o -lgcc

POPD

