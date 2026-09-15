#include <efi.h>
#include <efilib.h>
#include <stdint.h>

extern const unsigned char _binary_build_boot_raw_start[];
extern const unsigned char _binary_build_boot_raw_end[];

typedef struct {
    uint32_t w;
    uint32_t h;
    UINT32 stride;
    EFI_GRAPHICS_PIXEL_FORMAT format;
    EFI_PIXEL_BITMASK mask;
    uint32_t *fb;
} SCREEN;

static SCREEN screen;

static uint32_t read_u32(const unsigned char *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t scale_channel(uint8_t value, uint32_t mask) {
    if (!mask) return 0;
    uint32_t shift = 0;
    while (((mask >> shift) & 1u) == 0u) shift++;
    uint32_t bits = 0;
    while (((mask >> (shift + bits)) & 1u) != 0u) bits++;
    uint32_t max = (1u << bits) - 1u;
    return (((uint32_t)value * max + 127u) / 255u) << shift;
}

static uint32_t pack_pixel(EFI_GRAPHICS_PIXEL_FORMAT format, EFI_PIXEL_BITMASK mask,
                           uint8_t r, uint8_t g, uint8_t b) {
    if (format == PixelBlueGreenRedReserved8BitPerColor)
        return ((uint32_t)b) | ((uint32_t)g << 8) | ((uint32_t)r << 16);
    if (format == PixelRedGreenBlueReserved8BitPerColor)
        return ((uint32_t)r) | ((uint32_t)g << 8) | ((uint32_t)b << 16);
    return scale_channel(r, mask.RedMask) |
           scale_channel(g, mask.GreenMask) |
           scale_channel(b, mask.BlueMask);
}

static void put_pixel(int x, int y, uint32_t colour) {
    if (x < 0 || y < 0 || (UINT32)x >= screen.w || (UINT32)y >= screen.h) return;
    screen.fb[(uint64_t)y * screen.stride + x] = colour;
}

static void fill_rect(int x, int y, int w, int h, uint32_t colour) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)screen.w) w = (int)screen.w - x;
    if (y + h > (int)screen.h) h = (int)screen.h - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) put_pixel(xx, yy, colour);
}

/* Tiny 5x7 font for the desktop UI. */
static const uint8_t font[26][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,14}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
};

static void draw_char(int x, int y, char c, int scale, uint32_t colour) {
    if (c == ' ') return;
    if (c < 'A' || c > 'Z') return;
    int index = c - 'A';
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if (font[index][row] & (1 << (4 - col)))
                fill_rect(x + col * scale, y + row * scale, scale, scale, colour);
        }
    }
}

static void draw_text(int x, int y, const char *text, int scale, uint32_t colour) {
    while (*text) {
        draw_char(x, y, *text++, scale, colour);
        x += 6 * scale;
    }
}

static void draw_image(void) {
    const unsigned char *raw = _binary_build_boot_raw_start;
    const unsigned char *raw_end = _binary_build_boot_raw_end;
    if (raw_end - raw < 8) return;

    uint32_t src_w = read_u32(raw);
    uint32_t src_h = read_u32(raw + 4);
    const unsigned char *pixels = raw + 8;
    uint64_t required = (uint64_t)src_w * src_h * 4;
    if (src_w == 0 || src_h == 0 || (uint64_t)(raw_end - pixels) < required) return;

    UINT32 draw_w = screen.w;
    UINT32 draw_h = (UINT32)((uint64_t)src_h * screen.w / src_w);
    if (draw_h > screen.h) {
        draw_h = screen.h;
        draw_w = (UINT32)((uint64_t)src_w * screen.h / src_h);
    }
    UINT32 off_x = (screen.w - draw_w) / 2;
    UINT32 off_y = (screen.h - draw_h) / 2;

    for (UINT32 y = 0; y < screen.h; y++) {
        for (UINT32 x = 0; x < screen.w; x++) {
            uint8_t r = 0, g = 0, b = 0;
            if (x >= off_x && x < off_x + draw_w && y >= off_y && y < off_y + draw_h) {
                UINT32 sx = (UINT32)((uint64_t)(x - off_x) * src_w / draw_w);
                UINT32 sy = (UINT32)((uint64_t)(y - off_y) * src_h / draw_h);
                const unsigned char *p = pixels + ((uint64_t)sy * src_w + sx) * 4;
                b = p[0]; g = p[1]; r = p[2];
            }
            put_pixel((int)x, (int)y, pack_pixel(screen.format, screen.mask, r, g, b));
        }
    }
}

static void draw_desktop(void) {
    fill_rect(0, 0, screen.w, screen.h, pack_pixel(screen.format, screen.mask, 24, 30, 42));

    draw_text(32, 28, "STEVEOS", 5, pack_pixel(screen.format, screen.mask, 255, 255, 255));
    draw_text(32, 78, "PRESS S TO OPEN START", 2,
              pack_pixel(screen.format, screen.mask, 190, 200, 215));

    fill_rect(0, (int)screen.h - 64, screen.w, 64,
              pack_pixel(screen.format, screen.mask, 18, 22, 32));
    fill_rect(12, (int)screen.h - 52, 150, 40,
              pack_pixel(screen.format, screen.mask, 45, 55, 75));
    draw_text(38, (int)screen.h - 43, "START", 3,
              pack_pixel(screen.format, screen.mask, 255, 255, 255));
}

static void draw_start_menu(int selected) {
    int menu_w = 360;
    int menu_h = 200;
    int x = 12;
    int y = (int)screen.h - 64 - menu_h - 8;
    uint32_t panel = pack_pixel(screen.format, screen.mask, 30, 36, 50);
    uint32_t border = pack_pixel(screen.format, screen.mask, 75, 88, 115);
    uint32_t white = pack_pixel(screen.format, screen.mask, 255, 255, 255);
    uint32_t muted = pack_pixel(screen.format, screen.mask, 180, 190, 205);
    uint32_t highlight = pack_pixel(screen.format, screen.mask, 65, 95, 145);

    fill_rect(x, y, menu_w, menu_h, panel);
    fill_rect(x, y, menu_w, 3, border);
    draw_text(x + 24, y + 18, "STEVEOS", 3, white);
    draw_text(x + 24, y + 48, "APPS", 2, muted);

    if (selected == 0) fill_rect(x + 14, y + 72, menu_w - 28, 48, highlight);
    draw_text(x + 30, y + 82, "BLEHHH", 3, white);
    draw_text(x + 30, y + 106, "OPEN THE PICTURE", 1, muted);

    if (selected == 1) fill_rect(x + 14, y + 126, menu_w - 28, 48, highlight);
    draw_text(x + 30, y + 138, "SHUT DOWN", 2, white);
}

static void redraw(int start_open, int selected) {
    draw_desktop();
    if (start_open) draw_start_menu(selected);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    InitializeLib(image_handle, system_table);

    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS status = uefi_call_wrapper(BS->LocateProtocol, 3,
                                          &gEfiGraphicsOutputProtocolGuid, NULL, (void **)&gop);
    if (EFI_ERROR(status) || !gop) return status;

    UINT32 best_mode = gop->Mode->Mode;
    UINT32 best_area = 0;
    for (UINT32 mode = 0; mode < gop->Mode->MaxMode; mode++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
        UINTN info_size = 0;
        status = uefi_call_wrapper(gop->QueryMode, 4, gop, mode, &info_size, &info);
        if (EFI_ERROR(status)) continue;
        UINT32 area = info->HorizontalResolution * info->VerticalResolution;
        if (area > best_area) {
            best_area = area;
            best_mode = mode;
        }
    }
    uefi_call_wrapper(gop->SetMode, 2, gop, best_mode);

    screen.w = gop->Mode->Info->HorizontalResolution;
    screen.h = gop->Mode->Info->VerticalResolution;
    screen.stride = gop->Mode->Info->PixelsPerScanLine;
    screen.format = gop->Mode->Info->PixelFormat;
    screen.mask = gop->Mode->Info->PixelInformation;
    screen.fb = (uint32_t *)(UINTN)gop->Mode->FrameBufferBase;

    EFI_INPUT_KEY key;
    int start_open = 0;
    int selected = 0;
    redraw(start_open, selected);

    while (1) {
        status = uefi_call_wrapper(BS->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, NULL);
        if (EFI_ERROR(status)) continue;
        status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key);
        if (EFI_ERROR(status)) continue;

        if (key.UnicodeChar == 's' || key.UnicodeChar == 'S') {
            start_open = !start_open;
            selected = 0;
            redraw(start_open, selected);
            continue;
        }

        if (!start_open) continue;

        if (key.ScanCode == SCAN_UP) {
            selected = 0;
            redraw(1, selected);
        } else if (key.ScanCode == SCAN_DOWN) {
            selected = 1;
            redraw(1, selected);
        } else if (key.ScanCode == SCAN_ESC) {
            start_open = 0;
            redraw(0, selected);
        } else if (key.UnicodeChar == CHAR_CARRIAGE_RETURN || key.UnicodeChar == ' ') {
            if (selected == 0) {
                draw_image();
                start_open = 0;
                while (1) {
                    status = uefi_call_wrapper(BS->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, NULL);
                    if (EFI_ERROR(status)) continue;
                    status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key);
                    if (!EFI_ERROR(status)) break;
                }
                redraw(0, 0);
            }
        }
    }

    return EFI_SUCCESS;
}
