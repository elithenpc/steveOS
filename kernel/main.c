#include <stdint.h>
#include <stddef.h>
#include "../src/bootinfo.h"

#define EFI_CONVENTIONAL_MEMORY 7

extern void native_reboot(void);
extern void native_halt(void);
extern uint8_t native_keyboard_read_scancode(void);

extern const unsigned char _binary_build_boot_raw_start[];
extern const unsigned char _binary_build_boot_raw_end[];

static uint32_t *framebuffer;
static uint32_t fb_width;
static uint32_t fb_height;
static uint32_t fb_stride;
static uint32_t fb_format;
static uint64_t memory_total;
static uint64_t conventional_total;
static uint64_t heap_current;
static uint64_t heap_end;

static const uint8_t font[36][5] = {
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

static inline void pixel(int x, int y, uint32_t c) {
    if (!framebuffer || x < 0 || y < 0 || (uint32_t)x >= fb_width || (uint32_t)y >= fb_height)
        return;
    framebuffer[(size_t)y * fb_stride + (size_t)x] = c;
}

static void fill(int x, int y, int w, int h, uint32_t c) {
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx)
            pixel(x + xx, y + yy, c);
}

static const uint8_t *glyph_for(char c) {
    if (c >= 'A' && c <= 'Z') return font[c - 'A'];
    if (c >= 'a' && c <= 'z') return font[c - 'a'];
    if (c >= '0' && c <= '9') return font[26 + c - '0'];
    return NULL;
}

static void glyph(int x, int y, char c, uint32_t color, int scale) {
    const uint8_t *g = glyph_for(c);
    if (!g) return;
    for (int col = 0; col < 5; ++col)
        for (int bit = 0; bit < 7; ++bit)
            if (g[col] & (1u << bit))
                fill(x + col * scale, y + bit * scale, scale, scale, color);
}

static void text(int x, int y, const char *s, uint32_t color, int scale) {
    while (*s) {
        if (*s == ' ') x += 6 * scale;
        else { glyph(x, y, *s, color, scale); x += 6 * scale; }
        ++s;
    }
}

static void number(int x, int y, uint64_t value, uint32_t color, int scale) {
    char buf[24];
    size_t n = 0;
    if (!value) buf[n++] = '0';
    while (value && n < sizeof(buf) - 1) {
        buf[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n) glyph(x, y, buf[--n], color, scale), x += 6 * scale;
}

static uint64_t native_page_alloc(void) {
    if (!heap_current || heap_current + 4096 > heap_end)
        return 0;
    uint64_t page = heap_current;
    heap_current += 4096;
    return page;
}

static void memory_init(const STEVEOS_BOOT_INFO *boot) {
    memory_total = 0;
    conventional_total = 0;
    heap_current = 0;
    heap_end = 0;

    if (!boot || !boot->memory_map || !boot->memory_descriptor_size)
        return;

    uint8_t *p = (uint8_t *)(uintptr_t)boot->memory_map;
    uint8_t *end = p + boot->memory_map_size;
    uint64_t largest_start = 0;
    uint64_t largest_end = 0;

    while (p + boot->memory_descriptor_size <= end) {
        uint64_t *q = (uint64_t *)p;
        uint32_t type = *(uint32_t *)p;
        uint64_t physical_start = q[1];
        uint64_t pages = q[4];
        uint64_t bytes = pages * 4096ULL;

        if (type == EFI_CONVENTIONAL_MEMORY) {
            conventional_total += bytes;
            uint64_t region_end = physical_start + bytes;
            if (region_end > largest_end && bytes >= 1024 * 1024) {
                largest_start = physical_start;
                largest_end = region_end;
            }
        }

        p += boot->memory_descriptor_size;
    }

    memory_total = conventional_total;
    heap_current = (largest_start + 0xFFFULL) & ~0xFFFULL;
    heap_end = largest_end;
}

static int acpi_present(const STEVEOS_BOOT_INFO *boot) {
    return boot && boot->acpi_rsdp != 0;
}

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (fb_format == 0)
        return ((uint32_t)r << 0) | ((uint32_t)g << 8) | ((uint32_t)b << 16);
    return ((uint32_t)b << 0) | ((uint32_t)g << 8) | ((uint32_t)r << 16);
}

static void draw_image_card(void) {
    const uint8_t *raw = _binary_build_boot_raw_start;
    const uint8_t *raw_end = _binary_build_boot_raw_end;
    if ((size_t)(raw_end - raw) < 8)
        return;

    uint32_t iw = *(const uint32_t *)(raw + 0);
    uint32_t ih = *(const uint32_t *)(raw + 4);
    const uint8_t *pixels = raw + 8;
    size_t required = (size_t)iw * ih * 4 + 8;
    if (!iw || !ih || required > (size_t)(raw_end - raw))
        return;

    uint32_t dw = 360;
    uint32_t dh = (uint64_t)ih * dw / iw;
    if (dh > 420) {
        dh = 420;
        dw = (uint64_t)iw * dh / ih;
    }

    int ox = (int)fb_width - (int)dw - 70;
    int oy = 150 + (420 - (int)dh) / 2;
    fill(ox - 12, 138, (int)dw + 24, 444, rgb(20, 30, 50));

    for (uint32_t y = 0; y < dh; ++y) {
        uint32_t sy = (uint64_t)y * ih / dh;
        for (uint32_t x = 0; x < dw; ++x) {
            uint32_t sx = (uint64_t)x * iw / dw;
            const uint8_t *v = pixels + ((size_t)sy * iw + sx) * 4;
            pixel(ox + (int)x, oy + (int)y, rgb(v[2], v[1], v[0]));
        }
    }
}

static void draw_desktop(const STEVEOS_BOOT_INFO *boot) {
    fill(0, 0, (int)fb_width, (int)fb_height, rgb(7, 12, 24));
    fill(0, 0, (int)fb_width, 12, rgb(94, 141, 255));
    fill(0, 52, (int)fb_width, 2, rgb(45, 72, 115));

    fill(24, 18, 34, 34, rgb(94, 141, 255));
    text(33, 30, "S", rgb(255, 255, 255), 2);
    text(74, 25, "STEVEOS", rgb(245, 248, 255), 2);
    text(151, 28, "NATIVE KERNEL", rgb(166, 184, 213), 1);

    fill((int)fb_width - 210, 18, 180, 32, rgb(23, 35, 57));
    text((int)fb_width - 193, 29, "KERNEL ONLINE", rgb(220, 255, 232), 1);

    text(48, 95, "STEVEOS NATIVE KERNEL", rgb(245, 248, 255), 4);
    text(50, 135, "UEFI BOOT SERVICES RELEASED", rgb(164, 183, 211), 1);

    fill(48, 180, 520, 86, rgb(17, 27, 46));
    text(70, 198, "MEMORY", rgb(160, 180, 210), 1);
    number(70, 225, memory_total / 1024 / 1024, rgb(245, 248, 255), 2);
    text(160, 227, "MB", rgb(160, 180, 210), 1);
    text(280, 198, "HEAP READY", rgb(160, 180, 210), 1);
    number(280, 225, (heap_end > heap_current ? heap_end - heap_current : 0) / 1024 / 1024, rgb(245, 248, 255), 2);
    text(370, 227, "MB", rgb(160, 180, 210), 1);

    fill(48, 290, 520, 150, rgb(17, 27, 46));
    text(70, 312, "KERNEL STATUS", rgb(160, 180, 210), 1);
    fill(70, 348, 10, 10, rgb(98, 212, 138));
    text(92, 349, "GDT INITIALISED", rgb(238, 245, 255), 1);
    fill(70, 376, 10, 10, rgb(98, 212, 138));
    text(92, 377, "IDT INITIALISED", rgb(238, 245, 255), 1);
    fill(70, 404, 10, 10, rgb(98, 212, 138));
    text(92, 405, acpi_present(boot) ? "ACPI TABLE FOUND" : "ACPI TABLE NOT FOUND", rgb(238, 245, 255), 1);

    draw_image_card();

    fill(48, (int)fb_height - 104, (int)fb_width - 96, 58, rgb(17, 27, 46));
    text(70, (int)fb_height - 84, "NATIVE MODE", rgb(94, 141, 255), 1);
    text(188, (int)fb_height - 84, "UEFI SERVICES OFF", rgb(166, 184, 213), 1);
    text(380, (int)fb_height - 84, "KERNEL OWNS CPU", rgb(166, 184, 213), 1);
}

static char scan_to_ascii(uint8_t code) {
    static const char map[128] = {
        0,27,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,
        'q','w','e','r','t','y','u','i','o','p','[',']',0,0,'a','s',
        'd','f','g','h','j','k','l',';', '\'', '`',0,'\\','z','x','c','v',
        'b','n','m',',','.','/',0,'*',0,' ',0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };
    if (code >= 128) return 0;
    return map[code];
}

static void native_terminal(const STEVEOS_BOOT_INFO *boot) {
    char line[80];
    size_t len = 0;
    int y = (int)fb_height - 170;
    text(70, y, "KERNEL CONSOLE: TYPE HELP", rgb(166, 184, 213), 1);
    text(70, y + 26, ">", rgb(94, 141, 255), 1);

    for (;;) {
        uint8_t scan = native_keyboard_read_scancode();
        if (!scan) {
            __asm__ __volatile__("pause");
            continue;
        }
        if (scan & 0x80)
            continue;

        char c = scan_to_ascii(scan);
        if (c == '\n' || c == '\r') {
            line[len] = 0;
            if (line[0] == 'H' && line[1] == 'E' && line[2] == 'L' && line[3] == 'P' && !line[4]) {
                text(70, y + 48, "HELP MEM KERNEL CLEAR REBOOT HALT", rgb(235, 241, 252), 1);
            } else if (line[0] == 'M' && line[1] == 'E' && line[2] == 'M' && !line[3]) {
                text(70, y + 48, "CONVENTIONAL MEMORY", rgb(166, 184, 213), 1);
                number(230, y + 48, conventional_total / 1024 / 1024, rgb(235, 241, 252), 1);
                text(275, y + 48, "MB", rgb(166, 184, 213), 1);
            } else if (line[0] == 'K' && line[1] == 'E' && line[2] == 'R' && line[3] == 'N' && line[4] == 'E' && line[5] == 'L' && !line[6]) {
                text(70, y + 48, "POST UEFI NATIVE KERNEL RUNNING", rgb(98, 212, 138), 1);
            } else if (line[0] == 'C' && line[1] == 'L' && line[2] == 'E' && line[3] == 'A' && line[4] == 'R' && !line[5]) {
                fill(60, y + 42, (int)fb_width - 120, 60, rgb(17, 27, 46));
            } else if (line[0] == 'R' && line[1] == 'E' && line[2] == 'B' && line[3] == 'O' && line[4] == 'O' && line[5] == 'T' && !line[6]) {
                native_reboot();
            } else if (line[0] == 'H' && line[1] == 'A' && line[2] == 'L' && line[3] == 'T' && !line[4]) {
                native_halt();
            }
            len = 0;
            line[0] = 0;
            fill(168, y + 22, (int)fb_width - 260, 16, rgb(17, 27, 46));
            text(70, y + 26, ">", rgb(94, 141, 255), 1);
            continue;
        }

        if (c == '\b') {
            if (len) {
                --len;
                fill(168 + (int)len * 6, y + 22, 8, 16, rgb(17, 27, 46));
            }
            continue;
        }

        if (c >= 32 && c < 127 && len < sizeof(line) - 1) {
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            line[len++] = c;
            line[len] = 0;
            glyph(168 + (int)(len - 1) * 6, y + 26, c, rgb(245, 248, 255), 1);
        }
    }
}

void kernel_main(const STEVEOS_BOOT_INFO *boot) {
    if (!boot || boot->magic != STEVEOS_BOOT_MAGIC) {
        native_halt();
    }

    framebuffer = (uint32_t *)(uintptr_t)boot->framebuffer_base;
    fb_width = (uint32_t)boot->width;
    fb_height = (uint32_t)boot->height;
    fb_stride = (uint32_t)boot->pixels_per_scanline;
    fb_format = (uint32_t)boot->pixel_format;

    memory_init(boot);
    draw_desktop(boot);
    native_terminal(boot);
    native_halt();
}
