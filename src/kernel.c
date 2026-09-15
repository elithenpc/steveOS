#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include "kernel.h"
#include "bootinfo.h"

extern const unsigned char _binary_build_native_kernel_raw_start[];
extern const unsigned char _binary_build_native_kernel_raw_end[];

typedef void (*STEVEOS_NATIVE_ENTRY)(STEVEOS_BOOT_INFO *boot, void *stack_top);
#define STEVEOS_KERNEL_LOAD_ADDRESS 0x00200000ULL

static EFI_STATUS get_memory_map(EFI_MEMORY_DESCRIPTOR **map,
                                 UINTN *map_size,
                                 UINTN *map_key,
                                 UINTN *descriptor_size,
                                 UINT32 *descriptor_version) {
    UINTN size = 0;
    UINTN key = 0;
    UINTN desc_size = 0;
    UINT32 version = 0;
    EFI_STATUS st;

    st = uefi_call_wrapper(BS->GetMemoryMap, 5,
                           &size, NULL, &key, &desc_size, &version);
    if (st != EFI_BUFFER_TOO_SMALL || desc_size == 0)
        return st;

    size += desc_size * 4;
    *map = AllocatePool(size);
    if (!*map)
        return EFI_OUT_OF_RESOURCES;

    st = uefi_call_wrapper(BS->GetMemoryMap, 5,
                           &size, *map, &key, &desc_size, &version);
    if (EFI_ERROR(st)) {
        FreePool(*map);
        *map = NULL;
        return st;
    }

    *map_size = size;
    *map_key = key;
    *descriptor_size = desc_size;
    *descriptor_version = version;
    return EFI_SUCCESS;
}

static EFI_PHYSICAL_ADDRESS allocate_kernel_pages(UINTN pages) {
    EFI_PHYSICAL_ADDRESS address = STEVEOS_KERNEL_LOAD_ADDRESS;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePages, 4,
                                      AllocateAddress,
                                      EfiLoaderData,
                                      pages,
                                      &address);
    return EFI_ERROR(st) ? 0 : address;
}

static EFI_PHYSICAL_ADDRESS allocate_pages(UINTN pages) {
    EFI_PHYSICAL_ADDRESS address = 0;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePages, 4,
                                      AllocateAnyPages,
                                      EfiLoaderData,
                                      pages,
                                      &address);
    return EFI_ERROR(st) ? 0 : address;
}

static UINT64 find_acpi_rsdp(void) {
    static EFI_GUID acpi20 =
        {0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}};
    static EFI_GUID acpi10 =
        {0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

    UINT64 fallback = 0;
    for (UINTN i = 0; i < ST->NumberOfTableEntries; ++i) {
        EFI_CONFIGURATION_TABLE *entry = &ST->ConfigurationTable[i];
        if (!CompareGuid(&entry->VendorGuid, &acpi20))
            return (UINT64)(UINTN)entry->VendorTable;
        if (!CompareGuid(&entry->VendorGuid, &acpi10))
            fallback = (UINT64)(UINTN)entry->VendorTable;
    }
    return fallback;
}

EFI_STATUS steveos_kernel_prepare(void) {
    const UINTN raw_size = (UINTN)(_binary_build_native_kernel_raw_end -
                                    _binary_build_native_kernel_raw_start);
    return raw_size ? EFI_SUCCESS : EFI_NOT_FOUND;
}

EFI_STATUS steveos_kernel_boot(EFI_HANDLE image_handle,
                               EFI_GRAPHICS_OUTPUT_PROTOCOL *gop) {
    if (!gop || !gop->Mode || !gop->Mode->Info)
        return EFI_INVALID_PARAMETER;

    if (EFI_ERROR(steveos_kernel_prepare()))
        return EFI_NOT_FOUND;

    const UINTN kernel_size = (UINTN)(_binary_build_native_kernel_raw_end -
                                      _binary_build_native_kernel_raw_start);
    const UINTN kernel_pages = (kernel_size + 4095) / 4096;
    const UINTN stack_pages = 16;

    EFI_PHYSICAL_ADDRESS kernel_addr = allocate_kernel_pages(kernel_pages);
    EFI_PHYSICAL_ADDRESS stack_addr = allocate_pages(stack_pages);
    STEVEOS_BOOT_INFO *boot = AllocatePool(sizeof(STEVEOS_BOOT_INFO));

    if (!kernel_addr || !stack_addr || !boot) {
        if (kernel_addr) uefi_call_wrapper(BS->FreePages, 2, kernel_addr, kernel_pages);
        if (stack_addr) uefi_call_wrapper(BS->FreePages, 2, stack_addr, stack_pages);
        if (boot) FreePool(boot);
        return EFI_OUT_OF_RESOURCES;
    }

    CopyMem((VOID *)(UINTN)kernel_addr,
            _binary_build_native_kernel_raw_start,
            kernel_size);

    ZeroMem(boot, sizeof(*boot));
    boot->magic = STEVEOS_BOOT_MAGIC;
    boot->framebuffer_base = gop->Mode->FrameBufferBase;
    boot->framebuffer_size = gop->Mode->FrameBufferSize;
    boot->width = gop->Mode->Info->HorizontalResolution;
    boot->height = gop->Mode->Info->VerticalResolution;
    boot->pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;
    boot->pixel_format = gop->Mode->Info->PixelFormat;
    boot->acpi_rsdp = find_acpi_rsdp();
    boot->kernel_base = kernel_addr;
    boot->kernel_size = kernel_size;
    boot->kernel_stack_top = stack_addr + stack_pages * 4096ULL - 16;

    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;

    EFI_STATUS st = get_memory_map(&map, &map_size, &map_key,
                                   &descriptor_size, &descriptor_version);
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(BS->FreePages, 2, kernel_addr, kernel_pages);
        uefi_call_wrapper(BS->FreePages, 2, stack_addr, stack_pages);
        FreePool(boot);
        return st;
    }

    boot->memory_map = (UINT64)(UINTN)map;
    boot->memory_map_size = map_size;
    boot->memory_descriptor_size = descriptor_size;
    boot->memory_descriptor_version = descriptor_version;

    st = uefi_call_wrapper(BS->ExitBootServices, 2, image_handle, map_key);
    if (EFI_ERROR(st)) {
        FreePool(map);
        map = NULL;
        st = get_memory_map(&map, &map_size, &map_key,
                            &descriptor_size, &descriptor_version);
        if (EFI_ERROR(st))
            return st;
        boot->memory_map = (UINT64)(UINTN)map;
        boot->memory_map_size = map_size;
        boot->memory_descriptor_size = descriptor_size;
        boot->memory_descriptor_version = descriptor_version;
        st = uefi_call_wrapper(BS->ExitBootServices, 2, image_handle, map_key);
        if (EFI_ERROR(st))
            return st;
    }

    STEVEOS_NATIVE_ENTRY entry = (STEVEOS_NATIVE_ENTRY)(UINTN)kernel_addr;
    entry(boot, (VOID *)(UINTN)boot->kernel_stack_top);

    for (;;) __asm__ __volatile__("cli; hlt");
}

EFI_STATUS steveos_kernel_handoff(void) {
    return EFI_UNSUPPORTED;
}

void steveos_kernel_panic(const CHAR8 *message) {
    (void)message;
    for (;;) __asm__ __volatile__("cli; hlt");
}
