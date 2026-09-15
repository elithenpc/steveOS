#ifndef STEVEOS_MEMORY_H
#define STEVEOS_MEMORY_H

#include <efi.h>

EFI_STATUS steveos_memory_init(void);
UINT64 steveos_total_memory_bytes(void);
void steveos_memory_shutdown(void);

#endif
