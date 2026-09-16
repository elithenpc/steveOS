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
#define DW_TXFLR 0x74
#define DW_RXFLR 0x78
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

typedef struct __attribute__((packed)) {
    uint16_t hid_desc_len;
    uint16_t bcd_version;
    uint16_t report_desc_len;
    uint16_t report_desc_reg;
    uint16_t input_reg;
    uint16_t max_input_len;
    uint16_t output_reg;
    uint16_t max_output_len;
    uint16_t command_reg;
    uint16_t data_reg;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t version_id;
    uint16_t reserved;
} HID_DESC;

static volatile uint8_t *base;
static uint8_t ready;
static uint8_t device_ready;
static uint16_t address;
static HID_DESC hid;
static uint8_t report_desc[512];
static uint16_t report_len;
static uint8_t input_buf[128];
static uint16_t max_input;

extern void native_pointer_move(int32_t dx, int32_t dy, uint8_t buttons);

static inline uint32_t in32(uint16_t p) {
    uint32_t v;
    __asm__ __volatile__("inl %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline void out32(uint16_t p, uint32_t v) {
    __asm__ __volatile__("outl %0,%1" : : "a"(v), "Nd"(p));
}
static inline void pause_cpu(void) { __asm__ __volatile__("pause"); }

static uint32_t pci_read32(uint8_t b, uint8_t d, uint8_t f, uint8_t o) {
    uint32_t a = 0x80000000u | ((uint32_t)b << 16) | ((uint32_t)d << 11) |
                 ((uint32_t)f << 8) | (o & 0xFCu);
    out32(PCI_ADDR, a);
    return in32(PCI_DATA);
}
static void pci_write32(uint8_t b, uint8_t d, uint8_t f, uint8_t o, uint32_t v) {
    uint32_t a = 0x80000000u | ((uint32_t)b << 16) | ((uint32_t)d << 11) |
                 ((uint32_t)f << 8) | (o & 0xFCu);
    out32(PCI_ADDR, a);
    out32(PCI_DATA, v);
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
static int bus_idle(uint32_t timeout) {
    while (timeout--) {
        uint32_t s = *(volatile uint32_t *)(base + DW_STATUS);
        if (!(s & 1u) && (s & DW_STATUS_TFE)) return 1;
        pause_cpu();
    }
    return 0;
}

/* HID-over-I2C uses a two-byte little-endian register address followed by the
 * requested data. DesignWare permits the address write and reads in one FIFO
 * transaction using RESTART on the first read command. */
static int i2c_read_register(uint16_t reg, uint8_t *out, size_t len) {
    if (!ready || !out || !len || len > 127) return 0;
    *(volatile uint32_t *)(base + DW_TAR) = address;
    (void)*(volatile uint32_t *)(base + DW_CLR_INTR);
    if (!wait_tx(50000)) return 0;

    *(volatile uint32_t *)(base + DW_DATA_CMD) = (uint32_t)(reg & 0xFFu);
    if (!wait_tx(50000)) return 0;
    *(volatile uint32_t *)(base + DW_DATA_CMD) = (uint32_t)(reg >> 8);

    for (size_t i = 0; i < len; ++i) {
        if (!wait_tx(50000)) return 0;
        uint32_t cmd = DW_DATA_READ;
        if (i == 0) cmd |= DW_DATA_RESTART;
        if (i + 1 == len) cmd |= DW_DATA_STOP;
        *(volatile uint32_t *)(base + DW_DATA_CMD) = cmd;
    }
    for (size_t i = 0; i < len; ++i) {
        if (!wait_rx(50000)) return 0;
        out[i] = (uint8_t)*(volatile uint32_t *)(base + DW_DATA_CMD);
    }
    return bus_idle(50000);
}

static int find_controller(uint64_t *bar_out) {
    for (uint32_t b = 0; b < 256; ++b) {
        for (uint32_t d = 0; d < 32; ++d) {
            for (uint32_t f = 0; f < 8; ++f) {
                uint32_t id = pci_read32((uint8_t)b, (uint8_t)d, (uint8_t)f, 0);
                if ((uint16_t)id == 0xFFFFu) continue;
                uint32_t command = pci_read32((uint8_t)b, (uint8_t)d, (uint8_t)f, 4);
                pci_write32((uint8_t)b, (uint8_t)d, (uint8_t)f, 4, command | 6u);
                uint32_t lo = pci_read32((uint8_t)b, (uint8_t)d, (uint8_t)f, 0x10);
                if (lo & 1u) continue;
                uint64_t bar = (uint64_t)(lo & 0xFFFFFFF0u);
                if (((lo >> 1) & 3u) == 2u)
                    bar |= (uint64_t)pci_read32((uint8_t)b, (uint8_t)d, (uint8_t)f, 0x14) << 32;
                if (!bar) continue;
                if (*(volatile uint32_t *)(uintptr_t)(bar + DW_COMP_TYPE) == 0x44570140u) {
                    *bar_out = bar;
                    return 1;
                }
            }
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
    static const uint16_t hid_descriptor_regs[] = {1, 0};
    uint8_t raw[HID_DESC_LEN];
    for (uint16_t a = 0x08; a <= 0x5F; ++a) {
        address = a;
        for (size_t r = 0; r < sizeof(hid_descriptor_regs) / sizeof(hid_descriptor_regs[0]); ++r) {
            if (!i2c_read_register(hid_descriptor_regs[r], raw, sizeof(raw))) continue;
            HID_DESC *h = (HID_DESC *)raw;
            if (h->hid_desc_len != sizeof(HID_DESC)) continue;
            if (!h->input_reg || h->max_input_len < 4 || h->max_input_len > sizeof(input_buf)) continue;
            if (!h->report_desc_reg || !h->report_desc_len) continue;
            hid = *h;
            max_input = h->max_input_len;
            device_ready = 1;
            return 1;
        }
    }
    return 0;
}

int native_i2c_hid_init(void) {
    uint64_t bar = 0;
    ready = device_ready = 0;
    report_len = 0;
    if (!find_controller(&bar)) return 0;
    if (!controller_start(bar)) return 0;
    if (!find_hid_device()) return 0;
    report_len = hid.report_desc_len;
    if (report_len > sizeof(report_desc)) report_len = sizeof(report_desc);
    if (report_len)
        (void)i2c_read_register(hid.report_desc_reg, report_desc, report_len);
    return device_ready;
}

void native_i2c_hid_poll(void) {
    if (!device_ready) return;
    if (!i2c_read_register(hid.input_reg, input_buf, max_input)) return;
    uint16_t declared = (uint16_t)input_buf[0] | ((uint16_t)input_buf[1] << 8);
    if (declared < 3 || declared > max_input) return;

    /* Many ELAN firmware builds expose a mouse-compatible relative report.
     * Keep this deliberately conservative while the absolute multitouch HID
     * parser is built. */
    uint16_t off = 2;
    if (report_len && report_desc[0] == 0x85 && declared > off) ++off;
    if ((uint16_t)(declared - off) >= 3) {
        uint8_t buttons = input_buf[off];
        int32_t dx = (int8_t)input_buf[off + 1];
        int32_t dy = (int8_t)input_buf[off + 2];
        if (dx || dy || buttons)
            native_pointer_move(dx, -dy, buttons);
    }
}

int native_i2c_hid_present(void) { return device_ready != 0; }
