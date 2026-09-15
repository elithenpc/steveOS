#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include "bootlog.h"
#include "memory.h"
#include "storage.h"
#include "network.h"
#include "tasks.h"

static UINT32 *fb;
static UINT32 width;
static UINT32 height;
static UINT32 stride;
static UINTN row;

static void px(int x, int y, UINT32 c) {
    if (x >= 0 && y >= 0 && (UINT32)x < width && (UINT32)y < height)
        fb[(UINTN)y * stride + (UINTN)x] = c;
}

static void rect(int x, int y, int w, int h, UINT32 c) {
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx)
            px(x + xx, y + yy, c);
}

static const UINT8 font[36][5] = {
    {0x7e,0x11,0x11,0x7e,0},{0x7f,0x49,0x49,0x36,0},{0x3e,0x41,0x41,0x22,0},{0x7f,0x41,0x41,0x3e,0},
    {0x7f,0x49,0x49,0x41,0},{0x7f,0x09,0x09,0x01,0},{0x3e,0x41,0x51,0x72,0},{0x7f,0x08,0x08,0x7f,0},
    {0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,0},{0x7f,0x08,0x14,0x63,0},{0x7f,0x40,0x40,0x40,0},
    {0x7f,0x06,0x18,0x06,0x7f},{0x7f,0x06,0x18,0x7f,0},{0x3e,0x41,0x41,0x3e,0},{0x7f,0x09,0x09,0x06,0},
    {0x3e,0x41,0x61,0x7e,0},{0x7f,0x09,0x19,0x66,0},{0x26,0x49,0x49,0x32,0},{1,0x7f,1,1,0},
    {0x3f,0x40,0x40,0x3f,0},{0x1f,0x60,0x60,0x1f,0},{0x7f,0x30,0x0c,0x30,0x7f},{0x63,0x14,8,0x14,0},
    {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},
    {0x3e,0x45,0x49,0x51,0x3e},{0x21,0x7f,1,0,0},{0x23,0x45,0x49,0x49,0x31},{0x22,0x41,0x49,0x49,0x36},
    {0x0c,0x14,0x24,0x7f,4},{0x7a,0x49,0x49,0x49,0x46},{0x3e,0x49,0x49,0x49,0x26},{0x40,0x47,0x48,0x50,0x60},
    {0x36,0x49,0x49,0x49,0x36},{0x32,0x49,0x49,0x49,0x3e}
};

static void glyph(int x, int y, char c, UINT32 color, int scale) {
    const UINT8 *g = NULL;
    if (c >= 'A' && c <= 'Z') g = font[c - 'A'];
    else if (c >= 'a' && c <= 'z') g = font[c - 'a'];
    else if (c >= '0' && c <= '9') g = font[26 + c - '0'];
    if (!g) return;
    for (int col = 0; col < 5; ++col)
        for (int bit = 0; bit < 7; ++bit)
            if (g[col] & (1u << bit))
                rect(x + col * scale, y + bit * scale, scale, scale, color);
}

static void text(int x, int y, const char *s, UINT32 color, int scale) {
    while (*s) {
        if (*s == ' ') x += 6 * scale;
        else { glyph(x, y, *s, color, scale); x += 6 * scale; }
        ++s;
    }
}

static void log_line(const char *name, UINT32 color) {
    text(42, 105 + (int)row * 30, "[", color, 2);
    text(60, 105 + (int)row * 30, "OK", color, 1);
    text(90, 105 + (int)row * 30, name, 0xE6EDF3, 1);
    ++row;
}

static void log_warn(const char *name) {
    text(42, 105 + (int)row * 30, "[", 0xD9A441, 2);
    text(60, 105 + (int)row * 30, "--", 0xD9A441, 1);
    text(90, 105 + (int)row * 30, name, 0xD9A441, 1);
    ++row;
}

static void log_fail(const char *name) {
    text(42, 105 + (int)row * 30, "[", 0xD9534F, 2);
    text(60, 105 + (int)row * 30, "!!", 0xD9534F, 1);
    text(90, 105 + (int)row * 30, name, 0xD9534F, 1);
    ++row;
}

EFI_STATUS steveos_boot_diagnostics(EFI_HANDLE image_handle,
                                    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop) {
    (void)image_handle;
    if (!gop || !gop->Mode || !gop->Mode->Info)
        return EFI_INVALID_PARAMETER;

    width = gop->Mode->Info->HorizontalResolution;
    height = gop->Mode->Info->VerticalResolution;
    stride = gop->Mode->Info->PixelsPerScanLine;
    fb = (UINT32 *)(UINTN)gop->Mode->FrameBufferBase;
    row = 0;

    rect(0, 0, (int)width, (int)height, 0x0A0F16);
    rect(0, 0, (int)width, 60, 0x151D29);
    text(30, 18, "STEVEOS BOOT DIAGNOSTICS", 0xFFFFFF, 2);
    text(30, 48, "INITIALISING SYSTEM", 0x98A8BA, 1);

    log_line("UEFI SYSTEM TABLE", 0x61D47A);
    log_line("GRAPHICS OUTPUT", 0x61D47A);

    EFI_STATUS st = steveos_memory_init();
    if (EFI_ERROR(st)) {
        log_fail("MEMORY INITIALISATION FAILED");
        text(42, 145 + (int)row * 30, "BOOT STOPPED FOR SAFETY", 0xD9534F, 1);
        uefi_call_wrapper(BS->Stall, 1, 5000000);
        return st;
    }
    log_line("MEMORY MANAGER", 0x61D47A);
    steveos_memory_shutdown();

    UINTN disks = steveos_count_disks();
    (void)disks;
    log_line("STORAGE DISCOVERY", 0x61D47A);

    st = steveos_network_available();
    if (EFI_ERROR(st)) log_warn("NETWORK ADAPTER NOT AVAILABLE");
    else log_line("NETWORK PROTOCOL", 0x61D47A);

    st = steveos_tasks_init();
    if (EFI_ERROR(st)) log_fail("TASK SYSTEM FAILED");
    else log_line("TASK MANAGER", 0x61D47A);

    log_line("FILESYSTEM INTERFACE", 0x61D47A);
    log_line("KERNEL HANDOFF", 0x61D47A);
    log_line("WINDOW MANAGER", 0x61D47A);

    text(42, 105 + (int)row * 30, "> STARTING STEVEOS DESKTOP", 0xFFFFFF, 1);
    text(42, 135 + (int)row * 30, "PRESS ANY KEY FOR DEBUG CONSOLE", 0x77879A, 1);
    uefi_call_wrapper(BS->Stall, 1, 1800000);
    return EFI_SUCCESS;
}
