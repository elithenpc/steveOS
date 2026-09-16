#include <stdint.h>
#include <stddef.h>
#include "i2c.h"

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC
#define DW_CON 0x00
#define DW_TAR 0x04
#define DW_DATA_CMD 0x10
#define DW_FS_HCNT 0x1C
#define DW_FS_LCNT 0x20
#define DW_INTR_MASK 0x30
#define DW_CLR_INTR 0x40
#define DW_ENABLE 0x6C
#define DW_STATUS 0x70
#define DW_TX_ABRT_SOURCE 0x80
#define DW_COMP_TYPE 0xFC
#define DW_CON_MASTER (1u << 0)
#define DW_CON_SPEED_FAST (2u << 1)
#define DW_CON_RESTART (1u << 5)
#define DW_CON_SLAVE_DISABLE (1u << 6)
#define DW_STATUS_TFE (1u << 2)
#define DW_STATUS_RFNE (1u << 3)
#define DW_DATA_READ (1u << 8)
#define DW_DATA_STOP (1u << 9)
#define DW_DATA_RESTART (1u << 10)
#define HID_DESC_LEN 30
#define HID_ITEM_LONG 0xFE

typedef struct __attribute__((packed)) {
    uint16_t hid_desc_len, bcd_version, report_desc_len, report_desc_reg;
    uint16_t input_reg, max_input_len, output_reg, max_output_len;
    uint16_t command_reg, data_reg, vendor_id, product_id, version_id, reserved;
} HID_DESC;

typedef struct {
    uint16_t bit;
    uint8_t size;
    uint8_t valid;
    uint8_t relative;
} HID_AXIS;

static volatile uint8_t *base;
static uint8_t ready, device_ready;
static uint16_t address;
static HID_DESC hid;
static uint8_t report_desc[1024];
static uint16_t report_len;
static uint8_t input_buf[256];
static uint16_t max_input;
static HID_AXIS x_axis, y_axis;
static uint16_t button_bit;
static uint8_t button_size;
static uint8_t have_axes, have_button, report_has_id;
static uint8_t have_last_abs;
static uint16_t last_x, last_y;
static uint8_t last_buttons;

extern void native_pointer_move(int32_t dx, int32_t dy, uint8_t buttons);

static inline uint32_t in32(uint16_t p) {
    uint32_t v; __asm__ __volatile__("inl %1,%0" : "=a"(v) : "Nd"(p)); return v;
}
static inline void out32(uint16_t p, uint32_t v) {
    __asm__ __volatile__("outl %0,%1" : : "a"(v), "Nd"(p));
}
static inline void pause_cpu(void) { __asm__ __volatile__("pause"); }

static uint32_t pci_read32(uint8_t b, uint8_t d, uint8_t f, uint8_t o) {
    uint32_t a = 0x80000000u | ((uint32_t)b << 16) | ((uint32_t)d << 11) |
                 ((uint32_t)f << 8) | (o & 0xFCu);
    out32(PCI_ADDR, a); return in32(PCI_DATA);
}
static void pci_write32(uint8_t b, uint8_t d, uint8_t f, uint8_t o, uint32_t v) {
    uint32_t a = 0x80000000u | ((uint32_t)b << 16) | ((uint32_t)d << 11) |
                 ((uint32_t)f << 8) | (o & 0xFCu);
    out32(PCI_ADDR, a); out32(PCI_DATA, v);
}

static int wait_tx(uint32_t timeout) {
    while (timeout--) {
        if (*(volatile uint32_t *)(base + DW_TX_ABRT_SOURCE)) return 0;
        if (*(volatile uint32_t *)(base + DW_STATUS) & DW_STATUS_TFE) return 1;
        pause_cpu();
    }
    return 0;
}
static int wait_rx(uint32_t timeout) {
    while (timeout--) {
        if (*(volatile uint32_t *)(base + DW_TX_ABRT_SOURCE)) return 0;
        if (*(volatile uint32_t *)(base + DW_STATUS) & DW_STATUS_RFNE) return 1;
        pause_cpu();
    }
    return 0;
}

static int i2c_read_register(uint16_t reg, uint8_t *out, size_t len) {
    if (!ready || !out || !len || len > 255) return 0;
    *(volatile uint32_t *)(base + DW_TAR) = address;
    (void)*(volatile uint32_t *)(base + DW_CLR_INTR);
    if (!wait_tx(50000)) return 0;
    *(volatile uint32_t *)(base + DW_DATA_CMD) = (uint32_t)(reg & 0xFFu);
    if (!wait_tx(50000)) return 0;
    *(volatile uint32_t *)(base + DW_DATA_CMD) = (uint32_t)(reg >> 8);
    for (size_t i = 0; i < len; ++i) {
        if (!wait_tx(50000)) return 0;
        uint32_t cmd = DW_DATA_READ | (i ? 0 : DW_DATA_RESTART);
        if (i + 1 == len) cmd |= DW_DATA_STOP;
        *(volatile uint32_t *)(base + DW_DATA_CMD) = cmd;
    }
    for (size_t i = 0; i < len; ++i) {
        if (!wait_rx(50000)) return 0;
        out[i] = (uint8_t)*(volatile uint32_t *)(base + DW_DATA_CMD);
    }
    return 1;
}

static int find_controller(uint64_t *bar_out) {
    for (uint32_t b = 0; b < 256; ++b) for (uint32_t d = 0; d < 32; ++d)
        for (uint32_t f = 0; f < 8; ++f) {
            uint32_t id = pci_read32((uint8_t)b,(uint8_t)d,(uint8_t)f,0);
            if ((uint16_t)id == 0xFFFFu) continue;
            uint32_t cmd = pci_read32((uint8_t)b,(uint8_t)d,(uint8_t)f,4);
            pci_write32((uint8_t)b,(uint8_t)d,(uint8_t)f,4,cmd|6u);
            uint32_t lo = pci_read32((uint8_t)b,(uint8_t)d,(uint8_t)f,0x10);
            if (lo & 1u) continue;
            uint64_t bar = lo & 0xFFFFFFF0ULL;
            if (((lo >> 1) & 3u) == 2u)
                bar |= (uint64_t)pci_read32((uint8_t)b,(uint8_t)d,(uint8_t)f,0x14) << 32;
            if (!bar) continue;
            if (*(volatile uint32_t *)(uintptr_t)(bar + DW_COMP_TYPE) == 0x44570140u) {
                *bar_out = bar; return 1;
            }
        }
    return 0;
}

static int controller_start(uint64_t bar) {
    base = (volatile uint8_t *)(uintptr_t)bar;
    *(volatile uint32_t *)(base + DW_ENABLE) = 0;
    *(volatile uint32_t *)(base + DW_CON) = DW_CON_MASTER | DW_CON_SPEED_FAST |
                                             DW_CON_RESTART | DW_CON_SLAVE_DISABLE;
    *(volatile uint32_t *)(base + DW_FS_HCNT) = 160;
    *(volatile uint32_t *)(base + DW_FS_LCNT) = 160;
    *(volatile uint32_t *)(base + DW_INTR_MASK) = 0;
    *(volatile uint32_t *)(base + DW_ENABLE) = 1;
    ready = (*(volatile uint32_t *)(base + DW_ENABLE) & 1u) != 0;
    return ready;
}

static int find_hid_device(void) {
    uint8_t raw[HID_DESC_LEN];
    for (uint16_t a = 0x08; a <= 0x5F; ++a) {
        address = a;
        if (!i2c_read_register(1, raw, sizeof(raw))) continue;
        HID_DESC *h = (HID_DESC *)raw;
        if (h->hid_desc_len != HID_DESC_LEN) continue;
        if (!h->input_reg || !h->max_input_len || h->max_input_len > sizeof(input_buf)) continue;
        if (!h->report_desc_reg || !h->report_desc_len) continue;
        hid = *h; max_input = h->max_input_len; device_ready = 1; return 1;
    }
    return 0;
}

static uint32_t bits_get(const uint8_t *buf, uint16_t bit, uint8_t size) {
    uint32_t v = 0;
    for (uint8_t i = 0; i < size && i < 32; ++i)
        v |= ((uint32_t)((buf[(bit + i) >> 3] >> ((bit + i) & 7)) & 1u)) << i;
    return v;
}

static void parse_report(void) {
    x_axis.valid = y_axis.valid = 0;
    have_axes = have_button = report_has_id = 0;
    uint16_t bitpos = 0;
    uint32_t usage_page = 0, usage = 0;
    uint8_t report_size = 0, report_count = 0;
    uint8_t usages[8]; uint8_t usage_count = 0;
    size_t i = 0;
    while (i < report_len) {
        uint8_t prefix = report_desc[i++];
        if (!prefix) continue;
        if (prefix == HID_ITEM_LONG) {
            if (i + 2 > report_len) break;
            uint8_t n = report_desc[i++]; i += 1 + n; continue;
        }
        uint8_t sz = prefix & 3u; if (sz == 3) sz = 4;
        uint8_t type = (prefix >> 2) & 3u, tag = (prefix >> 4) & 0xFu;
        uint32_t value = 0;
        for (uint8_t n = 0; n < sz && i < report_len; ++n) value |= (uint32_t)report_desc[i++] << (8*n);
        if (type == 1) {
            if (tag == 0) usage_page = value;
            else if (tag == 7) report_size = (uint8_t)value;
            else if (tag == 8) { (void)value; }
            else if (tag == 9) report_count = (uint8_t)value;
            else if (tag == 8) {}
            if (tag == 8) bitpos = bitpos;
        } else if (type == 2 && tag == 0) {
            usage = value; if (usage_count < 8) usages[usage_count++] = (uint8_t)value;
        } else if (type == 0) {
            if (tag == 8) {
                uint8_t flags = (uint8_t)value;
                uint8_t count = report_count ? report_count : 1;
                for (uint8_t k = 0; k < count; ++k) {
                    uint32_t u = (k < usage_count) ? usages[k] : usage;
                    if (usage_page == 0x01 && u == 0x30 && !x_axis.valid)
                        x_axis = (HID_AXIS){bitpos,report_size,1,(uint8_t)((flags >> 2)&1)};
                    else if (usage_page == 0x01 && u == 0x31 && !y_axis.valid)
                        y_axis = (HID_AXIS){bitpos,report_size,1,(uint8_t)((flags >> 2)&1)};
                    else if (usage_page == 0x09 && u >= 1 && u <= 8 && !have_button) {
                        button_bit = bitpos; button_size = report_size; have_button = 1;
                    }
                    bitpos = (uint16_t)(bitpos + report_size);
                }
                usage_count = 0;
            } else if (tag == 9) {
                report_has_id = 1;
                bitpos = 0;
            } else if (tag == 10 || tag == 12) {
                usage_count = 0;
            }
        }
    }
    have_axes = x_axis.valid && y_axis.valid;
}

int native_i2c_hid_init(void) {
    uint64_t bar = 0;
    ready = device_ready = 0; report_len = 0; have_last_abs = 0;
    if (!find_controller(&bar)) return 0;
    if (!controller_start(bar)) return 0;
    if (!find_hid_device()) return 0;
    report_len = hid.report_desc_len;
    if (report_len > sizeof(report_desc)) report_len = sizeof(report_desc);
    if (!i2c_read_register(hid.report_desc_reg, report_desc, report_len)) return 0;
    parse_report();
    return device_ready;
}

void native_i2c_hid_poll(void) {
    if (!device_ready) return;
    if (!i2c_read_register(hid.input_reg, input_buf, max_input)) return;
    uint16_t declared = (uint16_t)input_buf[0] | ((uint16_t)input_buf[1] << 8);
    if (declared < 3 || declared > max_input) return;

    uint16_t report_base = report_has_id ? 24 : 16;
    if (have_axes && !x_axis.relative && !y_axis.relative) {
        uint32_t xv = bits_get(input_buf, (uint16_t)(x_axis.bit + report_base), x_axis.size);
        uint32_t yv = bits_get(input_buf, (uint16_t)(y_axis.bit + report_base), y_axis.size);
        if (!have_last_abs) {
            last_x = (uint16_t)xv; last_y = (uint16_t)yv; have_last_abs = 1;
        } else {
            int32_t dx = (int32_t)xv - (int32_t)last_x;
            int32_t dy = (int32_t)yv - (int32_t)last_y;
            last_x = (uint16_t)xv; last_y = (uint16_t)yv;
            uint8_t buttons = have_button ? (uint8_t)bits_get(input_buf, (uint16_t)(button_bit + report_base), button_size) : 0;
            if (dx || dy || buttons != last_buttons) {
                int32_t px = dx / 8; if (dx && !px) px = dx > 0 ? 1 : -1;
                int32_t py = dy / 8; if (dy && !py) py = dy > 0 ? 1 : -1;
                native_pointer_move(px, -py, buttons);
            }
            last_buttons = buttons;
        }
        return;
    }

    uint16_t off = 2 + (report_has_id ? 1 : 0);
    if ((uint16_t)(declared - off) >= 3) {
        uint8_t buttons = input_buf[off];
        int32_t dx = (int8_t)input_buf[off + 1];
        int32_t dy = (int8_t)input_buf[off + 2];
        if (dx || dy || buttons) native_pointer_move(dx, -dy, buttons);
    }
}

int native_i2c_hid_present(void) { return device_ready != 0; }
