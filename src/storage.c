#include <efi.h>
#include <efilib.h>
#include <stdint.h>

/* UEFI storage discovery. This is the bridge to the future FAT32/file system. */

UINTN steveos_count_disks(void) {
    UINTN count = 0;
    UINTN handles_size = 0;
    EFI_HANDLE *handles = NULL;
    EFI_STATUS status;

    status = uefi_call_wrapper(BS->LocateHandle, 5, ByProtocol,
                               &gEfiBlockIoProtocolGuid, NULL,
                               &handles_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL) return 0;

    handles = AllocatePool(handles_size);
    if (!handles) return 0;

    status = uefi_call_wrapper(BS->LocateHandle, 5, ByProtocol,
                               &gEfiBlockIoProtocolGuid, NULL,
                               &handles_size, handles);
    if (!EFI_ERROR(status)) {
        for (UINTN i = 0; i < handles_size / sizeof(EFI_HANDLE); i++) {
            EFI_BLOCK_IO_PROTOCOL *bio = NULL;
            if (!EFI_ERROR(uefi_call_wrapper(BS->HandleProtocol, 3,
                                             handles[i], &gEfiBlockIoProtocolGuid,
                                             (void **)&bio)) && bio && bio->Media &&
                bio->Media->LogicalPartition == FALSE && bio->Media->Present) {
                count++;
            }
        }
    }

    FreePool(handles);
    return count;
}
