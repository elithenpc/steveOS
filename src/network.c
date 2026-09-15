#include <efi.h>
#include <efilib.h>
#include <efinet.h>
#include "network.h"

/* UEFI Simple Network Protocol foundation. */
static EFI_SIMPLE_NETWORK *snp = NULL;

static EFI_GUID simple_network_guid = {
    0x5b1b31a1, 0x9562, 0x11d2,
    {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};

static EFI_STATUS network_start(void) {
    EFI_STATUS status = uefi_call_wrapper(BS->LocateProtocol, 3,
                                          &simple_network_guid, NULL,
                                          (void **)&snp);
    if (EFI_ERROR(status) || !snp)
        return status;

    if (snp->Mode && snp->Mode->State == EfiSimpleNetworkStopped) {
        status = uefi_call_wrapper(snp->Start, 2, snp);
        if (EFI_ERROR(status))
            return status;
    }

    return EFI_SUCCESS;
}

static void network_stop(void) {
    snp = NULL;
}

EFI_STATUS steveos_network_available(void) {
    EFI_STATUS status = network_start();
    if (EFI_ERROR(status))
        return status;

    network_stop();
    return EFI_SUCCESS;
}

EFI_STATUS steveos_http_test(void) {
    EFI_STATUS status = steveos_network_available();
    if (EFI_ERROR(status))
        return status;

    /* TCP/IP, DNS and HTTP will be owned by the native steveOS network stack. */
    return EFI_UNSUPPORTED;
}
