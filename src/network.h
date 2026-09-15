#ifndef STEVEOS_NETWORK_H
#define STEVEOS_NETWORK_H

#include <efi.h>

/* Returns EFI_SUCCESS when firmware exposes a usable Simple Network Protocol. */
EFI_STATUS steveos_network_available(void);

/* Legacy network test entry point retained for future TCP/IP and HTTP work. */
EFI_STATUS steveos_http_test(void);

#endif
