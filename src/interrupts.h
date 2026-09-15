#ifndef STEVEOS_INTERRUPTS_H
#define STEVEOS_INTERRUPTS_H

#include <efi.h>

EFI_STATUS steveos_interrupts_init(void);
void steveos_interrupts_disable(void);
void steveos_interrupts_enable(void);

#endif
