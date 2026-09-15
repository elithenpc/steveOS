#include <efi.h>
#include <efilib.h>
#include "fs.h"
#include "installer.h"

EFI_STATUS steveos_install_efi(EFI_HANDLE target_device,
                               VOID *efi_image, UINTN efi_size) {
    EFI_STATUS status;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *efi_dir = NULL;
    EFI_FILE_PROTOCOL *boot_dir = NULL;

    if (!target_device || !efi_image || !efi_size) return EFI_INVALID_PARAMETER;

    status = steveos_fs_open_volume(target_device, &root);
    if (EFI_ERROR(status)) return status;

    /* Create the EFI/BOOT directory hierarchy. Existing files are replaced only
       after the target volume has been explicitly selected by the installer. */
    status = uefi_call_wrapper(root->Open, 5, root, &efi_dir,
                               L"EFI", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                               EFI_FILE_MODE_CREATE, EFI_FILE_DIRECTORY);
    if (EFI_ERROR(status)) goto done;

    status = uefi_call_wrapper(efi_dir->Open, 5, efi_dir, &boot_dir,
                               L"BOOT", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                               EFI_FILE_MODE_CREATE, EFI_FILE_DIRECTORY);
    if (EFI_ERROR(status)) goto done;

    EFI_FILE_PROTOCOL *boot_file = NULL;
    status = uefi_call_wrapper(boot_dir->Open, 5, boot_dir, &boot_file,
                               L"BOOTX64.EFI",
                               EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                               EFI_FILE_MODE_CREATE, 0);
    if (!EFI_ERROR(status)) {
        UINTN write_size = efi_size;
        status = uefi_call_wrapper(boot_file->SetPosition, 2, boot_file, 0);
        if (!EFI_ERROR(status))
            status = uefi_call_wrapper(boot_file->Write, 3, boot_file,
                                       &write_size, efi_image);
        uefi_call_wrapper(boot_file->Close, 1, boot_file);
    }

done:
    if (boot_dir) uefi_call_wrapper(boot_dir->Close, 1, boot_dir);
    if (efi_dir) uefi_call_wrapper(efi_dir->Close, 1, efi_dir);
    if (root) uefi_call_wrapper(root->Close, 1, root);
    return status;
}
