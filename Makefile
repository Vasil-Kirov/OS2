SRC_DIR=src
BUILD_DIR=bin

ARCH=i686
AS=nasm
AR=$(ARCH)-elf-ar
CC=$(ARCH)-elf-gcc
ARCHDIR=$(SRC_DIR)/kernel/arch/$(ARCH)
ASM_FLAGS=-felf32
CFLAGS=-g -O0 -ffreestanding -Wall -Wextra -isystem=/usr/include -static -fno-pie
SYSROOT=$(PWD)/sysroot
KERNEL_CFLAGS:=$(CFLAGS) --sysroot=$(SYSROOT) -I$(SRC_DIR)/kernel
LIBK_CFLAGS:=$(CFLAGS) -D__is_libk --sysroot=$(SYSROOT)
ISO=$(BUILD_DIR)/VOS.iso
QEMU_FLAGS=-cdrom $(ISO) -device ahci,id=ahci -drive file=disk.qcow2,if=none,id=disk -device ide-hd,drive=disk,bus=ahci.0 -machine q35,acpi=on

LIBK_OBJS := \
	     $(BUILD_DIR)/strlen.o \
	     $(BUILD_DIR)/memcmp.o \
	     $(BUILD_DIR)/memcpy.o \


OBJS= \
	  $(BUILD_DIR)/crti.o \
	  $(BUILD_DIR)/boot.o \
	  $(BUILD_DIR)/kernel.o \
	  $(BUILD_DIR)/kmem.o \
	  $(BUILD_DIR)/io.o \
	  $(BUILD_DIR)/pci.o \
	  $(BUILD_DIR)/nvme.o \
	  $(BUILD_DIR)/acpi.o \
	  $(BUILD_DIR)/crtn.o \
	  #$(BUILD_DIR)/asmfn.o \

HEADERS=$(SRC_DIR)/kernel/include/kernel \
		$(ARCHDIR)/vga.h \
		$(SRC_DIR)/libc/include/stdio.h \
		$(SRC_DIR)/libc/include/string.h

.PHONY = all image kernel clean install install-headers install-kernel install-libs

verify: $(BUILD_DIR)/VOS.bin
	@if grub-file --is-x86-multiboot $(BUILD_DIR)/VOS.bin; then \
		echo "Confirmed!"; \
	else \
		echo "Invalid Multiboot."; \
	fi

run:
	qemu-system-i386 $(QEMU_FLAGS)

run_debug:
	qemu-system-i386 -M smm=off -d int $(QEMU_FLAGS)

run_gdb:
	qemu-system-i386 -M smm=off -s -S -d int $(QEMU_FLAGS)


iso: $(ISO)

$(SYSROOT)/usr/lib/libk.a: $(LIBK_OBJS)
	mkdir -pv $(SYSROOT)/usr/lib
	$(AR) rcs $@ $^

$(BUILD_DIR):
	mkdir -pv $(BUILD_DIR)

$(SYSROOT):
	mkdir -pv $(SYSROOT)
	mkdir -pv $(SYSROOT)/boot
	mkdir -pv $(SYSROOT)/usr/include
	mkdir -pv $(SYSROOT)/usr/lib

$(ISO): $(BUILD_DIR)/VOS.bin
	mkdir -p $(BUILD_DIR)/isodir/boot/grub
	cp $(BUILD_DIR)/VOS.bin $(BUILD_DIR)/isodir/boot/VOS.bin
	cp grub.cfg $(BUILD_DIR)/isodir/boot/grub/grub.cfg
	rm -f $(ISO)
	grub-mkrescue -o $(ISO) $(BUILD_DIR)/isodir

$(BUILD_DIR)/VOS.bin: $(SYSROOT) $(OBJS) $(SYSROOT)/usr/lib/libk.a
	$(CC) --sysroot=$(SYSROOT) -nostdlib -T linker.ld $(OBJS) -o $(BUILD_DIR)/VOS.bin -lk

clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(SYSROOT)

install: install-headers install-kernel install-libs 

install-libs: $(SYSROOT) $(BUILD_DIR)/crti.o $(BUILD_DIR)/crtn.o $(SYSROOT)/usr/lib/libk.a
	cp -R $(BUILD_DIR)/crti.o $(SYSROOT)/usr/lib/
	cp -R $(BUILD_DIR)/crtn.o $(SYSROOT)/usr/lib/


install-headers: $(SYSROOT)
	cp -R $(SRC_DIR)/libc/include/. $(SYSROOT)/usr/include/.

install-kernel: $(SYSROOT) $(BUILD_DIR)/VOS.bin
	cp $(BUILD_DIR)/VOS.bin $(SYSROOT)/boot

$(BUILD_DIR)/boot.o: $(ARCHDIR)/boot.s
	$(AS) $(ASM_FLAGS) $< -o $@

$(BUILD_DIR)/kernel.o: $(SRC_DIR)/kernel/kernel.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/kmem.o: $(SRC_DIR)/kernel/kmem.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/pci.o: $(SRC_DIR)/kernel/pci.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/acpi.o: $(SRC_DIR)/kernel/acpi.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/nvme.o: $(SRC_DIR)/kernel/drivers/nvme.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/io.o: $(SRC_DIR)/kernel/io.s
	$(AS) $(ASM_FLAGS) $< -o $@

$(BUILD_DIR)/crti.o: $(ARCHDIR)/crti.s
	$(CC) $(LIBK_CFLAGS) -c $< -o $@

$(BUILD_DIR)/crtn.o: $(ARCHDIR)/crtn.s
	$(CC) $(LIBK_CFLAGS) -c $< -o $@

$(BUILD_DIR)/strlen.o: $(SRC_DIR)/libc/string/strlen.c
	$(CC) $(LIBK_CFLAGS) -c $< -o $@

$(BUILD_DIR)/memcmp.o: $(SRC_DIR)/libc/string/memcmp.c
	$(CC) $(LIBK_CFLAGS) -c $< -o $@

$(BUILD_DIR)/memcpy.o: $(SRC_DIR)/libc/string/memcpy.c
	$(CC) $(LIBK_CFLAGS) -c $< -o $@

