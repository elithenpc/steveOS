#ifndef STEVEOS_BOOTLOG_H
#define STEVEOS_BOOTLOG_H

#include <efi.h>
#include <efilib.h>

EFI_STATUS steveos_boot_diagnostics(EFI_HANDLE image_handle,
                                    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);

#endif
