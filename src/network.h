#ifndef STEVEOS_NETWORK_H
#define STEVEOS_NETWORK_H

#include <efi.h>

/* Returns EFI_SUCCESS when UEFI exposes a network HTTP service. */
EFI_STATUS steveos_network_available(void);

/* Legacy network test entry point retained for compatibility. */
EFI_STATUS steveos_http_test(void);

/* Fetch a page through the firmware's UEFI HTTP stack on demand. */
EFI_STATUS steveos_http_get(const CHAR16 *url,
                            CHAR8 *out,
                            UINTN out_capacity,
                            UINTN *out_length,
                            UINT32 *http_status);

#endif
