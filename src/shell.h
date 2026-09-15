#ifndef STEVEOS_SHELL_H
#define STEVEOS_SHELL_H

#include <efi.h>
#include <efilib.h>

EFI_STATUS steveos_shell_start(EFI_HANDLE image_handle,
                               EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);

#endif
