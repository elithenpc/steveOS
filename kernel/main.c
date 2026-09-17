#include "desktop.h"

void kernel_main(STEVEOS_BOOT_INFO *boot) {
    steveos_desktop_init(boot);
    steveos_desktop_run(boot);
}
