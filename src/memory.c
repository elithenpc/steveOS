#include <efi.h>
#include <efilib.h>
#include <stdint.h>

/* Early memory manager. UEFI supplies the physical memory map; later the
 * native kernel can take ownership of it and implement paging/allocators. */

static UINTN memory_map_size;
static UINTN memory_map_key;
static UINTN memory_descriptor_size;
static UINT32 memory_descriptor_version;
static EFI_MEMORY_DESCRIPTOR *memory_map;
static UINT64 total_ram;

EFI_STATUS steveos_memory_init(void) {
    EFI_STATUS status;
    UINTN size = 0;

    status = uefi_call_wrapper(BS->GetMemoryMap, 5, &size, NULL,
                               &memory_map_key, &memory_descriptor_size,
                               &memory_descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL) return status;

    size += 2 * memory_descriptor_size;
    memory_map = AllocatePool(size);
    if (!memory_map) return EFI_OUT_OF_RESOURCES;

    memory_map_size = size;
    status = uefi_call_wrapper(BS->GetMemoryMap, 5, &memory_map_size,
                               memory_map, &memory_map_key,
                               &memory_descriptor_size,
                               &memory_descriptor_version);
    if (EFI_ERROR(status)) return status;

    total_ram = 0;
    for (UINTN offset = 0; offset < memory_map_size; offset += memory_descriptor_size) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)memory_map + offset);
        switch (d->Type) {
            case EfiLoaderCode:
            case EfiLoaderData:
            case EfiBootServicesCode:
            case EfiBootServicesData:
            case EfiConventionalMemory:
            case EfiACPIReclaimMemory:
                total_ram += d->NumberOfPages * 4096ULL;
                break;
            default:
                break;
        }
    }
    return EFI_SUCCESS;
}

UINT64 steveos_total_memory_bytes(void) {
    return total_ram;
}

void steveos_memory_shutdown(void) {
    if (memory_map) {
        FreePool(memory_map);
        memory_map = NULL;
    }
    memory_map_size = 0;
}
