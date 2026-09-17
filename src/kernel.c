#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include "kernel.h"
#include "bootinfo.h"
#include "network.h"

extern const unsigned char _binary_build_native_kernel_raw_start[];
extern const unsigned char _binary_build_native_kernel_raw_end[];
typedef void (*STEVEOS_NATIVE_ENTRY)(STEVEOS_BOOT_INFO *boot, void *stack_top);
#define STEVEOS_KERNEL_LOAD_ADDRESS 0x00200000ULL
#define STEVEOS_IMAGE_LOAD_LIMIT (2ULL * 1024ULL * 1024ULL)
#define STEVEOS_TEXT_LOAD_LIMIT (256ULL * 1024ULL)
#define EFI_FILE_DIRECTORY 0x10ULL

typedef struct {
    uint16_t name[STEVEOS_BOOT_FILE_NAME_MAX];
    uint64_t size;
    uint64_t data;
    uint32_t attributes;
    uint32_t kind;
} STEVEOS_BOOT_FILE_UEFI;

static EFI_STATUS get_memory_map(EFI_MEMORY_DESCRIPTOR **map,
                                 UINTN *map_size,
                                 UINTN *map_key,
                                 UINTN *descriptor_size,
                                 UINT32 *descriptor_version) {
    UINTN size = 0, key = 0, desc_size = 0;
    UINT32 version = 0;
    EFI_STATUS st = uefi_call_wrapper(BS->GetMemoryMap, 5,
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
                                      AllocateAddress, EfiLoaderData,
                                      pages, &address);
    return EFI_ERROR(st) ? 0 : address;
}

static EFI_PHYSICAL_ADDRESS allocate_pages(UINTN pages) {
    EFI_PHYSICAL_ADDRESS address = 0;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePages, 4,
                                      AllocateAnyPages, EfiLoaderData,
                                      pages, &address);
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

static int has_ext(const CHAR16 *name, const CHAR16 *ext) {
    UINTN n = StrLen((CHAR16 *)name), e = StrLen((CHAR16 *)ext);
    if (n < e) return 0;
    name += n - e;
    for (UINTN i = 0; i < e; ++i) {
        CHAR16 a = name[i], b = ext[i];
        if (a >= L'A' && a <= L'Z') a = (CHAR16)(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z') b = (CHAR16)(b - L'A' + L'a');
        if (a != b) return 0;
    }
    return 1;
}

static UINT32 file_kind(const CHAR16 *name) {
    if (has_ext(name, L".bmp") || has_ext(name, L".png") ||
        has_ext(name, L".jpg") || has_ext(name, L".jpeg") ||
        has_ext(name, L".ppm")) return 1;
    if (has_ext(name, L".txt") || has_ext(name, L".md") ||
        has_ext(name, L".log") || has_ext(name, L".html") || has_ext(name, L".htm") ||
        has_ext(name, L".css") || has_ext(name, L".json") || has_ext(name, L".xml") ||
        has_ext(name, L".csv") || has_ext(name, L".c") || has_ext(name, L".h") ||
        has_ext(name, L".hxx") || has_ext(name, L".cpp") || has_ext(name, L".py") ||
        has_ext(name, L".sh") || has_ext(name, L".ini") || has_ext(name, L".cfg")) return 2;
    return 0;
}

static EFI_STATUS snapshot_boot_files(EFI_HANDLE image_handle, STEVEOS_BOOT_INFO *boot) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_STATUS st;
    UINTN info_size;
    VOID *buf = NULL;

    if (!boot) return EFI_INVALID_PARAMETER;
    st = uefi_call_wrapper(BS->HandleProtocol, 3, image_handle,
                           &gEfiLoadedImageProtocolGuid, (VOID **)&loaded);
    if (EFI_ERROR(st) || !loaded || !loaded->DeviceHandle) return st;
    st = uefi_call_wrapper(BS->HandleProtocol, 3, loaded->DeviceHandle,
                           &gEfiSimpleFileSystemProtocolGuid, (VOID **)&fs);
    if (EFI_ERROR(st) || !fs) return st;
    st = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
    if (EFI_ERROR(st) || !root) return st;

    STEVEOS_BOOT_FILE_UEFI *files = AllocatePool(sizeof(*files) * STEVEOS_MAX_BOOT_FILES);
    if (!files) {
        uefi_call_wrapper(root->Close, 1, root);
        return EFI_OUT_OF_RESOURCES;
    }
    ZeroMem(files, sizeof(*files) * STEVEOS_MAX_BOOT_FILES);

    info_size = 4096;
    buf = AllocatePool(info_size);
    if (!buf) {
        FreePool(files);
        uefi_call_wrapper(root->Close, 1, root);
        return EFI_OUT_OF_RESOURCES;
    }

    uefi_call_wrapper(root->SetPosition, 2, root, 0);
    UINTN count = 0;
    while (count < STEVEOS_MAX_BOOT_FILES) {
        UINTN read_size = info_size;
        st = uefi_call_wrapper(root->Read, 3, root, &read_size, buf);
        if (EFI_ERROR(st) || read_size == 0) break;
        EFI_FILE_INFO *info = (EFI_FILE_INFO *)buf;
        if (read_size < sizeof(EFI_FILE_INFO) || info->Size > read_size) continue;

        STEVEOS_BOOT_FILE_UEFI *out = &files[count];
        UINTN j = 0;
        while (j + 1 < STEVEOS_BOOT_FILE_NAME_MAX && info->FileName[j]) {
            out->name[j] = info->FileName[j];
            ++j;
        }
        out->name[j] = 0;
        out->size = info->FileSize;
        out->attributes = (UINT32)info->Attribute;
        out->kind = (info->Attribute & EFI_FILE_DIRECTORY) ? 3u : file_kind(info->FileName);

        if (!(info->Attribute & EFI_FILE_DIRECTORY) &&
            ((out->kind == 1 && out->size > 0 && out->size <= STEVEOS_IMAGE_LOAD_LIMIT) ||
             (out->kind == 2 && out->size > 0 && out->size <= STEVEOS_TEXT_LOAD_LIMIT))) {
            EFI_FILE_PROTOCOL *file = NULL;
            st = uefi_call_wrapper(root->Open, 5, root, &file, info->FileName,
                                   EFI_FILE_MODE_READ, 0);
            if (!EFI_ERROR(st) && file) {
                UINTN pages = (UINTN)((out->size + 4095) / 4096);
                EFI_PHYSICAL_ADDRESS storage = allocate_pages(pages);
                if (storage) {
                    UINTN data_size = (UINTN)out->size;
                    st = uefi_call_wrapper(file->Read, 3, file, &data_size, (VOID *)(UINTN)storage);
                    if (!EFI_ERROR(st) && data_size == out->size)
                        out->data = storage;
                    else if (storage)
                        uefi_call_wrapper(BS->FreePages, 2, storage, pages);
                }
                uefi_call_wrapper(file->Close, 1, file);
            }
        }
        ++count;
    }

    boot->boot_files = (UINT64)(UINTN)files;
    boot->boot_file_count = count;
    boot->boot_device_handle = (UINT64)(UINTN)loaded->DeviceHandle;
    FreePool(buf);
    uefi_call_wrapper(root->Close, 1, root);
    return EFI_SUCCESS;
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
    UINT64 fb_bytes64 = (UINT64)gop->Mode->Info->PixelsPerScanLine *
                        (UINT64)gop->Mode->Info->VerticalResolution * 4ULL;
    const UINTN backbuffer_pages = (UINTN)((fb_bytes64 + 4095ULL) / 4096ULL);

    EFI_PHYSICAL_ADDRESS kernel_addr = allocate_kernel_pages(kernel_pages);
    EFI_PHYSICAL_ADDRESS stack_addr = allocate_pages(stack_pages);
    EFI_PHYSICAL_ADDRESS backbuffer_addr = allocate_pages(backbuffer_pages);
    STEVEOS_BOOT_INFO *boot = AllocatePool(sizeof(STEVEOS_BOOT_INFO));
    if (!kernel_addr || !stack_addr || !backbuffer_addr || !boot) {
        if (kernel_addr) uefi_call_wrapper(BS->FreePages, 2, kernel_addr, kernel_pages);
        if (stack_addr) uefi_call_wrapper(BS->FreePages, 2, stack_addr, stack_pages);
        if (backbuffer_addr) uefi_call_wrapper(BS->FreePages, 2, backbuffer_addr, backbuffer_pages);
        if (boot) FreePool(boot);
        return EFI_OUT_OF_RESOURCES;
    }

    CopyMem((VOID *)(UINTN)kernel_addr,
            _binary_build_native_kernel_raw_start, kernel_size);
    ZeroMem((VOID *)(UINTN)backbuffer_addr, (UINTN)fb_bytes64);
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
    boot->uefi_get_variable = (UINT64)(UINTN)RT->GetVariable;
    boot->uefi_set_variable = (UINT64)(UINTN)RT->SetVariable;
    boot->uefi_get_time = (UINT64)(UINTN)RT->GetTime;
    boot->uefi_http_get = (UINT64)(UINTN)steveos_http_get;
    boot->backbuffer_base = backbuffer_addr;
    boot->backbuffer_size = fb_bytes64;
    (void)snapshot_boot_files(image_handle, boot);

    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN map_size = 0, map_key = 0, descriptor_size = 0;
    UINT32 descriptor_version = 0;
    EFI_STATUS st = get_memory_map(&map, &map_size, &map_key,
                                   &descriptor_size, &descriptor_version);
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(BS->FreePages, 2, kernel_addr, kernel_pages);
        uefi_call_wrapper(BS->FreePages, 2, stack_addr, stack_pages);
        uefi_call_wrapper(BS->FreePages, 2, backbuffer_addr, backbuffer_pages);
        FreePool(boot);
        return st;
    }
    boot->memory_map = (UINT64)(UINTN)map;
    boot->memory_map_size = map_size;
    boot->memory_descriptor_size = descriptor_size;
    boot->memory_descriptor_version = descriptor_version;

    /* Keep UEFI Boot Services alive. The native desktop uses the firmware's
     * HTTP protocol as its network bridge, so ExitBootServices() would make
     * the browser impossible. The native framebuffer and input drivers still
     * own their hardware paths, while firmware services remain available for
     * explicit browser requests. */
    (void)map_key;

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
