#include <efi.h>
#include <efilib.h>
#include "kernel.h"
#include "memory.h"

/*
 * Kernel transition foundation.
 *
 * The current image is still a UEFI application, so we deliberately do not
 * call ExitBootServices yet. This module establishes the hand-off boundary
 * where the native kernel will take ownership of the machine.
 */

EFI_STATUS steveos_kernel_prepare(void) {
    return steveos_memory_init();
}

EFI_STATUS steveos_kernel_handoff(void) {
    /* Reserved for the real ExitBootServices + native entry transition. */
    return EFI_UNSUPPORTED;
}

void steveos_kernel_panic(const CHAR8 *message) {
    if (message)
        Print(L"\r\nSTEVEOS KERNEL PANIC: %a\r\n", message);

    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}
