#ifndef STEVEOS_NETWORK_H
#define STEVEOS_NETWORK_H

#include <efi.h>

/* Early Internet service. Uses UEFI networking until SteveOS owns its drivers. */
EFI_STATUS steveos_http_test(void);

#endif
