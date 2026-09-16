#ifndef STEVEOS_BOOTINFO_H
#define STEVEOS_BOOTINFO_H

#include <stdint.h>

#define STEVEOS_BOOT_MAGIC 0x53544556454F5331ULL

typedef struct {
    uint64_t magic;
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
    uint64_t width;
    uint64_t height;
    uint64_t pixels_per_scanline;
    uint64_t pixel_format;
    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_descriptor_size;
    uint64_t memory_descriptor_version;
    uint64_t acpi_rsdp;
    uint64_t kernel_base;
    uint64_t kernel_size;
    uint64_t kernel_stack_top;
    uint64_t uefi_get_variable;
    uint64_t uefi_set_variable;
} STEVEOS_BOOT_INFO;

#endif
