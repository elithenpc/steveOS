#ifndef STEVEOS_BOOTINFO_H
#define STEVEOS_BOOTINFO_H

#include <stdint.h>

#define STEVEOS_BOOT_MAGIC 0x53544556454F5331ULL
#define STEVEOS_BOOT_FILE_NAME_MAX 64
#define STEVEOS_MAX_BOOT_FILES 128

typedef struct {
    uint16_t name[STEVEOS_BOOT_FILE_NAME_MAX];
    uint64_t size;
    uint64_t data;
    uint32_t attributes;
    uint32_t kind;
} STEVEOS_BOOT_FILE;

typedef struct {
    uint64_t handle;
    uint64_t blocks;
    uint32_t block_size;
    uint8_t removable;
    uint8_t present;
    uint8_t filesystem;
    uint8_t reserved;
} STEVEOS_INSTALL_TARGET;

typedef struct {
    uint32_t state;
    uint32_t media_present;
    uint8_t mac[32];
    uint32_t mac_size;
    uint32_t reserved;
} STEVEOS_NETWORK_INFO;

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
    uint64_t uefi_get_time;
    uint64_t uefi_http_get;
    uint64_t uefi_write_text;
    uint64_t uefi_list_install_targets;
    uint64_t uefi_install_self;
    uint64_t uefi_install_server;
    uint64_t uefi_launch_server;
    uint64_t uefi_install_app;
    uint64_t uefi_download_app;
    uint64_t uefi_launch_app;
    uint64_t uefi_install_windows_app;
    uint64_t uefi_run_windows_app;
    uint64_t uefi_network_info;
    uint64_t boot_files;
    uint64_t boot_file_count;
    uint64_t boot_device_handle;
    uint64_t backbuffer_base;
    uint64_t backbuffer_size;
} STEVEOS_BOOT_INFO;

#endif
