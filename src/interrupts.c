#include <efi.h>
#include "interrupts.h"

/* Native IDT/PIC/APIC setup will live here after ExitBootServices. */

EFI_STATUS steveos_interrupts_init(void) {
    return EFI_UNSUPPORTED;
}

void steveos_interrupts_disable(void) {
    __asm__ __volatile__("cli");
}

void steveos_interrupts_enable(void) {
    __asm__ __volatile__("sti");
}
