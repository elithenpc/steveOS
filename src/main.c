#include <efi.h>
#include <efilib.h>
#include <stdint.h>

extern const unsigned char _binary_build_boot_raw_start[];
extern const unsigned char _binary_build_boot_raw_end[];

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
    if (format == PixelBlueGreenRedReserved8BitPerColor) {
        return ((uint32_t)b) | ((uint32_t)g << 8) | ((uint32_t)r << 16);
    }
    if (format == PixelRedGreenBlueReserved8BitPerColor) {
        return ((uint32_t)r) | ((uint32_t)g << 8) | ((uint32_t)b << 16);
    }
    return scale_channel(r, mask.RedMask) |
           scale_channel(g, mask.GreenMask) |
           scale_channel(b, mask.BlueMask);
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

    const unsigned char *raw = _binary_build_boot_raw_start;
    const unsigned char *raw_end = _binary_build_boot_raw_end;
    if (raw_end - raw < 8) return EFI_LOAD_ERROR;

    uint32_t src_w = read_u32(raw);
    uint32_t src_h = read_u32(raw + 4);
    const unsigned char *pixels = raw + 8;
    uint64_t required = (uint64_t)src_w * src_h * 4;
    if (src_w == 0 || src_h == 0 || (uint64_t)(raw_end - pixels) < required) return EFI_LOAD_ERROR;

    UINT32 dst_w = gop->Mode->Info->HorizontalResolution;
    UINT32 dst_h = gop->Mode->Info->VerticalResolution;
    UINT32 draw_w = dst_w;
    UINT32 draw_h = (UINT32)((uint64_t)src_h * dst_w / src_w);
    if (draw_h > dst_h) {
        draw_h = dst_h;
        draw_w = (UINT32)((uint64_t)src_w * dst_h / src_h);
    }
    UINT32 off_x = (dst_w - draw_w) / 2;
    UINT32 off_y = (dst_h - draw_h) / 2;

    UINT32 *fb = (UINT32 *)(UINTN)gop->Mode->FrameBufferBase;
    UINT32 stride = gop->Mode->Info->PixelsPerScanLine;
    EFI_GRAPHICS_PIXEL_FORMAT format = gop->Mode->Info->PixelFormat;
    EFI_PIXEL_BITMASK mask = gop->Mode->Info->PixelInformation;

    for (UINT32 y = 0; y < dst_h; y++) {
        for (UINT32 x = 0; x < dst_w; x++) {
            uint8_t r = 0, g = 0, b = 0;
            if (x >= off_x && x < off_x + draw_w && y >= off_y && y < off_y + draw_h) {
                UINT32 sx = (UINT32)((uint64_t)(x - off_x) * src_w / draw_w);
                UINT32 sy = (UINT32)((uint64_t)(y - off_y) * src_h / draw_h);
                const unsigned char *p = pixels + ((uint64_t)sy * src_w + sx) * 4;
                b = p[0]; g = p[1]; r = p[2];
            }
            fb[(uint64_t)y * stride + x] = pack_pixel(format, mask, r, g, b);
        }
    }

    while (1) {
        uefi_call_wrapper(BS->Stall, 1, 1000000);
    }

    return EFI_SUCCESS;
}
