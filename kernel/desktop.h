#ifndef STEVEOS_DESKTOP_H
#define STEVEOS_DESKTOP_H
#include "../src/bootinfo.h"

/* Internal helpers used by desktop.c before their definitions. */
static int terminal_file_match(const char *a, const char *b);

void steveos_desktop_init(STEVEOS_BOOT_INFO *boot);
void steveos_desktop_run(STEVEOS_BOOT_INFO *boot);
#endif
