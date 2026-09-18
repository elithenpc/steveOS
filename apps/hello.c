#include <efi.h>
#include <efilib.h>

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    InitializeLib(image_handle, system_table);
    Print(L"SteveOS Hello App is running.\r\n");
    Print(L"Press any key to return to SteveOS.\r\n");
    WaitForSingleEvent(system_table->ConIn->WaitForKey, 0);
    return EFI_SUCCESS;
}
