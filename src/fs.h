#ifndef STEVEOS_FS_H
#define STEVEOS_FS_H

#include <efi.h>

/* Early file system layer backed by UEFI Simple File System.
 * This currently gives SteveOS safe FAT/FAT32 file access while the native
 * kernel filesystem is being built. */
EFI_STATUS steveos_fs_open_volume(EFI_HANDLE device, EFI_FILE_PROTOCOL **root);
EFI_STATUS steveos_fs_read_file(EFI_FILE_PROTOCOL *root, CHAR16 *path,
                                 VOID **buffer, UINTN *size);
EFI_STATUS steveos_fs_write_file(EFI_FILE_PROTOCOL *root, CHAR16 *path,
                                  VOID *buffer, UINTN size);

#endif
