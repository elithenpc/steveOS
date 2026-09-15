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
static UINTN steps;

static void px(int x, int y, UINT32 c) {
    if (x >= 0 && y >= 0 && (UINT32)x < width && (UINT32)y < height)
        fb[(UINTN)y * stride + (UINTN)x] = c;
}

static void rect(int x, int y, int w, int h, UINT32 c) {
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx)
            px(x + xx, y + yy, c);
}

static void circle(int cx, int cy, int r, UINT32 c) {
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x)
            if (x*x + y*y <= r*r)
                px(cx + x, cy + y, c);
}

static const UINT8 font[36][5] = {
    {0x7e,0x11,0x11,0x7e,0},{0x7f,0x49,0x49,0x36,0},{0x3e,0x41,0x41,0x22,0},{0x7f,0x41,0x41,0x3e,0},
    {0x7f,0x49,0x49,0x41,0},{0x7f,0x09,0x09,0x01,0},{0x3e,0x41,0x51,0x72,0},{0x7f,0x08,0x08,0x7f,0},
    {0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,0},{0x7f,0x08,0x14,0x63,0},{0x7f,0x40,0x40,0x40,0},
    {0x7f,0x06,0x18,0x06,0x7f},{0x7f,0x06,0x18,0x7f,0},{0x3e,0x41,0x41,0x3e,0},{0x7f,0x09,0x09,0x06,0},
    {0x3e,0x41,0x61,0x7e,0},{0x7f,0x09,0x19,0x66,0},{0x26,0x49,0x49,0x32,0},{1,0x7f,1,1,0},
    {0x3f,0x40,0x40,0x3f,0},{0x1f,0x60,0x60,0x1f,0},{0x7f,0x30,0x0c,0x30,0x7f},{0x63,0x14,8,0x14,0x63},
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

static void progress(void) {
    int x = 42;
    int y = 492;
    int w = (int)width - 84;
    int fill = steps >= 8 ? w : (w * (int)steps) / 8;
    rect(x, y, w, 6, 0x202B3D);
    if (fill > 0) rect(x, y, fill, 6, 0x5E8DFF);
}

static void stage(const char *name, UINT32 color) {
    int y = 180 + (int)row * 36;
    rect(42, y - 4, (int)width - 84, 28, 0x111A2D);
    circle(56, y + 10, 5, color);
    text(74, y + 4, name, 0xF1F5FC, 1);
    ++row;
    ++steps;
    progress();
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
    steps = 0;

    rect(0, 0, (int)width, (int)height, 0x080D17);
    rect(0, 0, (int)width, 12, 0x5E8DFF);
    circle((int)width - 50, 58, 14, 0x17243A);
    circle((int)width - 50, 58, 6, 0x5E8DFF);

    text(42, 48, "STEVEOS", 0xFFFFFF, 4);
    text(44, 103, "BOOT SEQUENCE", 0x8FA3BF, 1);
    text(44, 125, "INITIALISING SYSTEM COMPONENTS", 0xC9D5E5, 2);

    rect(42, 156, (int)width - 84, 2, 0x27354A);

    EFI_STATUS st;

    stage("UEFI SYSTEM TABLE", 0x62D48A);
    stage("GRAPHICS OUTPUT", 0x62D48A);

    st = steveos_memory_init();
    if (EFI_ERROR(st)) {
        stage("MEMORY INITIALISATION FAILED", 0xD9534F);
        text(42, 525, "BOOT STOPPED FOR SAFETY", 0xD9534F, 1);
        uefi_call_wrapper(BS->Stall, 1, 5000000);
        return st;
    }
    stage("MEMORY MANAGER", 0x62D48A);
    steveos_memory_shutdown();

    (void)steveos_count_disks();
    stage("STORAGE DISCOVERY", 0x62D48A);

    st = steveos_network_available();
    stage(EFI_ERROR(st) ? "NETWORK DRIVER DEFERRED" : "NETWORK PROTOCOL", 0xD9A441);

    st = steveos_tasks_init();
    if (EFI_ERROR(st)) stage("TASK SYSTEM FAILED", 0xD9534F);
    else stage("TASK MANAGER", 0x62D48A);

    stage("FILESYSTEM INTERFACE", 0x62D48A);
    stage("KERNEL HANDOFF READY", 0x62D48A);
    stage("WINDOW MANAGER READY", 0x62D48A);

    progress();
    text(42, 525, "DESKTOP READY", 0x62D48A, 2);
    text(42, 556, "STARTING STEVEOS", 0x8FA3BF, 1);
    uefi_call_wrapper(BS->Stall, 1, 700000);
    return EFI_SUCCESS;
}
