#include <efi.h>
#include <efilib.h>
#include "bootlog.h"
#include "kernel.h"

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    InitializeLib(image_handle, system_table);
    Print(L"\r\nSTEVEOS: bootloader entry\r\n");

    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS status = uefi_call_wrapper(BS->LocateProtocol, 3,
                                          &gEfiGraphicsOutputProtocolGuid,
                                          NULL, (VOID **)&gop);
    if (EFI_ERROR(status) || !gop) {
        Print(L"STEVEOS: ERROR locating GOP: %r\r\n", status);
        return status;
    }
    Print(L"STEVEOS: GOP found\r\n");

    UINT32 best_mode = gop->Mode->Mode;
    UINT64 best_area = 0;
    for (UINT32 mode = 0; mode < gop->Mode->MaxMode; ++mode) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
        UINTN info_size = 0;
        status = uefi_call_wrapper(gop->QueryMode, 4, gop, mode,
                                   &info_size, &info);
        if (EFI_ERROR(status) || !info)
            continue;
        UINT64 area = (UINT64)info->HorizontalResolution * info->VerticalResolution;
        if (area > best_area) {
            best_area = area;
            best_mode = mode;
        }
        FreePool(info);
    }

    Print(L"STEVEOS: setting graphics mode %u\r\n", best_mode);
    status = uefi_call_wrapper(gop->SetMode, 2, gop, best_mode);
    if (EFI_ERROR(status)) {
        Print(L"STEVEOS: ERROR setting graphics mode: %r\r\n", status);
        return status;
    }

    status = steveos_boot_diagnostics(image_handle, gop);
    if (EFI_ERROR(status)) {
        Print(L"STEVEOS: BOOT DIAGNOSTICS FAILED: %r\r\n", status);
        return status;
    }

    Print(L"STEVEOS: loading native kernel\r\n");
    status = steveos_kernel_boot(image_handle, gop);
    if (EFI_ERROR(status)) {
        Print(L"STEVEOS: NATIVE KERNEL BOOT FAILED: %r\r\n", status);
        return status;
    }

    return EFI_SUCCESS;
}
