#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include "fs.h"

EFI_STATUS steveos_fs_open_volume(EFI_HANDLE device, EFI_FILE_PROTOCOL **root) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
    EFI_STATUS status;

    if (!root) return EFI_INVALID_PARAMETER;
    *root = NULL;

    status = uefi_call_wrapper(BS->HandleProtocol, 3, device,
                               &gEfiSimpleFileSystemProtocolGuid,
                               (void **)&sfs);
    if (EFI_ERROR(status) || !sfs) return status;

    return uefi_call_wrapper(sfs->OpenVolume, 2, sfs, root);
}

EFI_STATUS steveos_fs_read_file(EFI_FILE_PROTOCOL *root, CHAR16 *path,
                                VOID **buffer, UINTN *size) {
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_STATUS status;
    EFI_FILE_INFO *info = NULL;
    UINTN info_size = 0;
    VOID *data = NULL;
    UINTN data_size;

    if (!root || !path || !buffer || !size) return EFI_INVALID_PARAMETER;
    *buffer = NULL;
    *size = 0;

    status = uefi_call_wrapper(root->Open, 5, root, &file, path,
                               EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) return status;

    status = uefi_call_wrapper(file->GetInfo, 4, file,
                               &gEfiFileInfoGuid, &info_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL) goto done;

    info = AllocatePool(info_size);
    if (!info) { status = EFI_OUT_OF_RESOURCES; goto done; }

    status = uefi_call_wrapper(file->GetInfo, 4, file,
                               &gEfiFileInfoGuid, &info_size, info);
    if (EFI_ERROR(status)) goto done;

    data_size = (UINTN)info->FileSize;
    data = AllocatePool(data_size ? data_size : 1);
    if (!data) { status = EFI_OUT_OF_RESOURCES; goto done; }

    status = uefi_call_wrapper(file->Read, 3, file, &data_size, data);
    if (EFI_ERROR(status)) { FreePool(data); data = NULL; goto done; }

    *buffer = data;
    *size = data_size;

done:
    if (info) FreePool(info);
    if (file) uefi_call_wrapper(file->Close, 1, file);
    return status;
}

EFI_STATUS steveos_fs_write_file(EFI_FILE_PROTOCOL *root, CHAR16 *path,
                                 VOID *buffer, UINTN size) {
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_STATUS status;

    if (!root || !path || (!buffer && size)) return EFI_INVALID_PARAMETER;

    status = uefi_call_wrapper(root->Open, 5, root, &file, path,
                               EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                               EFI_FILE_MODE_CREATE, 0);
    if (EFI_ERROR(status)) return status;

    status = uefi_call_wrapper(file->SetPosition, 2, file, 0);
    if (!EFI_ERROR(status)) {
        UINTN write_size = size;
        status = uefi_call_wrapper(file->Write, 3, file, &write_size, buffer);
    }

    uefi_call_wrapper(file->Close, 1, file);
    return status;
}
