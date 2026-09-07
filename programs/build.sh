#!/bin/bash


nasm -felf32 hello_world.s -o hello_world.o
ld -m elf_i386 -static -nostdlib -o hello_world hello_world.o

