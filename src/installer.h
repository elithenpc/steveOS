#ifndef STEVEOS_INSTALLER_H
#define STEVEOS_INSTALLER_H

#include <efi.h>

/* Install an EFI payload onto a selected FAT/FAT32 EFI System Partition.
 * The caller is responsible for choosing the target device. */
EFI_STATUS steveos_install_efi(EFI_HANDLE target_device,
                               VOID *efi_image, UINTN efi_size);

#endif
