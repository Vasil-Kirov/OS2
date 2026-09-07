SRC_DIR := src
BUILD_DIR := bin

ARCH := i686

AS := nasm
AR := $(ARCH)-elf-ar
CC := $(ARCH)-elf-gcc

SYSROOT := $(PWD)/sysroot
ARCHDIR := $(SRC_DIR)/kernel/arch/$(ARCH)

ASM_FLAGS := -felf32

CFLAGS := -g -O0 -std=gnu2x -ffreestanding -Wall -Wextra -isystem=/usr/include -static -fno-pie -masm=intel
KERNEL_CFLAGS := $(CFLAGS) --sysroot=$(SYSROOT) -I$(SRC_DIR)/kernel -I$(ARCHDIR)
LIBK_CFLAGS := $(CFLAGS) -D__is_libk --sysroot=$(SYSROOT)

LDFLAGS := -nostdlib -T linker.ld

ISO := $(BUILD_DIR)/VOS.iso
KERNEL := $(BUILD_DIR)/VOS.bin

QEMU_FLAGS := -cdrom $(ISO) -device nvme,drive=nvme0,serial=nvme0 -drive file=disk.img,if=none,id=nvme0,format=raw -machine q35,acpi=on -serial stdio

SYSROOT_HEADERS := $(SYSROOT)/usr/include/.installed

KERNEL_SRC := \
			  $(wildcard $(SRC_DIR)/kernel/*.c) \
			  $(wildcard $(SRC_DIR)/kernel/drivers/*.c) \
			  $(wildcard $(SRC_DIR)/kernel/mem/*.c) \
			  $(wildcard $(SRC_DIR)/kernel/proc/*.c) \
			  $(wildcard $(SRC_DIR)/kernel/syscall/*.c) \
			  $(wildcard $(SRC_DIR)/kernel/display/*.c) \
			  $(wildcard $(ARCHDIR)/*.c)

ASM_SRC := \
		   $(wildcard $(SRC_DIR)/kernel/*.s) \
		   $(wildcard $(ARCHDIR)/*.s)

LIBK_SRC := $(wildcard $(SRC_DIR)/libc/string/*.c)

KERNEL_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(KERNEL_SRC))
KERNEL_OBJS += $(patsubst $(SRC_DIR)/%.s,$(BUILD_DIR)/%.o,$(ASM_SRC))

LIBK_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIBK_SRC))


HEADERS=$(SRC_DIR)/kernel/include/kernel \
		$(ARCHDIR)/vga.h \
		$(SRC_DIR)/libc/include/stdio.h \
		$(SRC_DIR)/libc/include/string.h

.PHONY = all clean run run_gdb verify

all: $(ISO)

$(BUILD_DIR):
	mkdir -p $@

$(SYSROOT):
	mkdir -p $@/boot
	mkdir -p $@/usr/include
	mkdir -p $@/usr/lib

$(SYSROOT_HEADERS): | $(SYSROOT)
	cp -R $(SRC_DIR)/libc/include/. $(SYSROOT)/usr/include/
	touch $@

# C compilation
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(SYSROOT_HEADERS)
	mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -MMD -MP -c $< -o $@


# Assembly compilation
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.s
	mkdir -p $(dir $@)
	$(AS) $(ASM_FLAGS) $< -o $@


# libk compilation
$(BUILD_DIR)/libc/%.o: $(SRC_DIR)/libc/%.c
	mkdir -p $(dir $@)
	$(CC) $(LIBK_CFLAGS) -MMD -MP -c $< -o $@


verify: $(BUILD_DIR)/VOS.bin
	@if grub-file --is-x86-multiboot2 $(BUILD_DIR)/VOS.bin; then \
		echo "Confirmed!"; \
	else \
		echo "Invalid Multiboot."; \
	fi

run: $(ISO)
	qemu-system-i386 $(QEMU_FLAGS)

run_gdb: $(ISO)
	qemu-system-i386 -M smm=off -s -S -d int $(QEMU_FLAGS)


$(SYSROOT)/usr/lib/libk.a: $(LIBK_OBJS) | $(SYSROOT)
	$(AR) rcs $@ $^

$(KERNEL): $(KERNEL_OBJS) $(SYSROOT)/usr/lib/libk.a | $(SYSROOT)
	$(CC) $(LDFLAGS) $(KERNEL_OBJS) --sysroot=$(SYSROOT) -lk -lgcc -o $@

$(ISO): $(KERNEL)
	mkdir -p $(BUILD_DIR)/isodir/boot/grub
	cp $(KERNEL) $(BUILD_DIR)/isodir/boot/VOS.bin
	cp grub.cfg $(BUILD_DIR)/isodir/boot/grub/
	grub-mkrescue -o $@ $(BUILD_DIR)/isodir

clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(SYSROOT)


DISK := $(PWD)/mnt
.PHONY: disk
disk:
	@echo "[INFO] Creating ext2 disk image from $(DISK)"
	dd if=/dev/zero of=disk.img bs=1M count=64 status=none
	mke2fs -q -F -t ext2 -d "$(DISK)" disk.img
	@echo "[SUCCESS] disk.img created"


-include $(DEPS)

