#ifndef STEVEOS_KERNEL_H
#define STEVEOS_KERNEL_H

#include <efi.h>

EFI_STATUS steveos_kernel_prepare(void);
EFI_STATUS steveos_kernel_boot(EFI_HANDLE image_handle,
                               EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);
EFI_STATUS steveos_kernel_handoff(void);
void steveos_kernel_panic(const CHAR8 *message);

#endif
