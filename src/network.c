#include <efi.h>
#include <efilib.h>
#include <efinet.h>
#include "network.h"

/*
 * Network foundation.
 *
 * Ubuntu's GNU-EFI 3.0.15 headers do not provide the UEFI HTTP protocol
 * definitions, so the earlier HTTP implementation could never compile on
 * the GitHub runner. We use the UEFI Simple Network Protocol here instead.
 *
 * This gives steveOS a real firmware-networking entry point and lets the
 * native TCP/IP implementation be added without making the UEFI build depend
 * on headers that GNU-EFI does not ship.
 */

static EFI_SIMPLE_NETWORK *snp = NULL;

static EFI_GUID simple_network_guid = {
    0x5b1b31a1, 0x9562, 0x11d2,
    {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};

static EFI_STATUS network_start(void) {
    EFI_STATUS status;

    status = uefi_call_wrapper(BS->LocateProtocol, 3,
                               &simple_network_guid, NULL,
                               (void **)&snp);
    if (EFI_ERROR(status) || !snp)
        return status;

    if (snp->Mode->State == EfiSimpleNetworkStopped) {
        status = uefi_call_wrapper(snp->Start, 2, snp);
        if (EFI_ERROR(status))
            return status;
    }

    return EFI_SUCCESS;
}

static void network_stop(void) {
    snp = NULL;
}

EFI_STATUS steveos_http_test(void) {
    EFI_STATUS status = network_start();
    if (EFI_ERROR(status))
        return status;

    /*
     * The firmware NIC is available here. HTTP/TCP is deliberately kept out
     * of this UEFI application until the steveOS network layer owns sockets.
     */
    network_stop();
    return EFI_UNSUPPORTED;
}
