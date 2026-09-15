#include <efi.h>
#include <efilib.h>
#include "network.h"

/*
 * Network support is deliberately deferred during the early UEFI boot path.
 * Some firmware implementations can block inside LocateProtocol/Start for a
 * network adapter, which prevents the desktop from ever starting. The native
 * network driver can safely probe hardware later after the OS is running.
 */

EFI_STATUS steveos_network_available(void) {
    return EFI_UNSUPPORTED;
}

EFI_STATUS steveos_http_test(void) {
    return EFI_UNSUPPORTED;
}
