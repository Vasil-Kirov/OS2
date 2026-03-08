
#ifndef _MULTIBOOT_H
#define _MULTIBOOT_H

#include <stdint.h>

typedef struct {
    uint32_t flags;           // Multiboot info version number
    uint32_t mem_lower;       // Available memory from BIOS
    uint32_t mem_upper;
    uint32_t boot_device;     // Boot device
    uint32_t cmdline;         // Kernel command line
    uint32_t mods_count;      // Boot-Module list
    uint32_t mods_addr;
    uint32_t syms[4];         // a.out symbol table or ELF section header table
    uint32_t mmap_length;     // Memory Mapping buffer
    uint32_t mmap_addr;
    uint32_t drives_length;   // Drive Info buffer
    uint32_t drives_addr;
    uint32_t config_table;    // ROM configuration table
    uint32_t boot_loader_name;// Boot Loader Name
    uint32_t apm_table;       // APM table
    uint32_t vbe_control_info;// VBE control information
    uint32_t vbe_mode_info;   // VBE mode information
    uint16_t vbe_mode;        // VBE mode
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
	uint64_t framebuffer_addr;
	uint32_t framebuffer_pitch;
	uint32_t framebuffer_width;
	uint32_t framebuffer_height;
	uint8_t  framebuffer_bpp;
	uint8_t  framebuffer_type;
} multiboot_info;

#define MULTIBOOT_MEMORY_AVAILABLE              1
#define MULTIBOOT_MEMORY_RESERVED               2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE       3
#define MULTIBOOT_MEMORY_NVS                    4
#define MULTIBOOT_MEMORY_BADRAM                 5
typedef struct __attribute__((packed)) {
   uint32_t size;
   uint32_t base_addr_low;
   uint32_t base_addr_high;
   uint32_t length_low;
   uint32_t length_high;
   uint32_t type;
} multiboot_memory_map_t;

#endif

