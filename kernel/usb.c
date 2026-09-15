#include <stdint.h>
#include <stddef.h>
#include "usb.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define XHCI_CAPLENGTH       0x00
#define XHCI_HCSPARAMS1      0x04
#define XHCI_HCSPARAMS2      0x08
#define XHCI_HCCPARAMS1      0x10
#define XHCI_DBOFF           0x14
#define XHCI_RTSOFF          0x18

#define XHCI_USBCMD          0x00
#define XHCI_USBSTS          0x04
#define XHCI_PAGESIZE        0x08
#define XHCI_CRCR            0x18
#define XHCI_DCBAAP          0x30
#define XHCI_CONFIG          0x38
#define XHCI_PORTSC_BASE     0x400
#define XHCI_PORTSC_STRIDE   0x10

#define XHCI_IMAN            0x20
#define XHCI_IMOD            0x24
#define XHCI_ERSTSZ          0x28
#define XHCI_ERSTBA         0x30
#define XHCI_ERDP            0x38

#define USB_CMD_RUN          (1u << 0)
#define USB_CMD_HCRST        (1u << 1)
#define USB_STS_HCH          (1u << 0)

#define PORTSC_CCS           (1u << 0)
#define PORTSC_PED           (1u << 1)
#define PORTSC_PR            (1u << 4)
#define PORTSC_PRC           (1u << 21)
#define PORTSC_SPEED_SHIFT   10
#define PORTSC_SPEED_MASK    0xFu

#define TRB_CYCLE             (1u << 0)
#define TRB_ENT               (1u << 1)
#define TRB_CHAIN             (1u << 4)
#define TRB_IOC               (1u << 5)
#define TRB_IDT               (1u << 6)
#define TRB_DIR_IN            (1u << 16)
#define TRB_LINK              6u
#define TRB_SETUP             2u
#define TRB_DATA              3u
#define TRB_STATUS            4u
#define TRB_ENABLE_SLOT       9u
#define TRB_ADDRESS_DEVICE    11u
#define TRB_CONFIGURE_EP      12u
#define TRB_NORMAL             1u
#define TRB_TRANSFER_EVENT    32u
#define TRB_COMPLETION        33u

#define CC_SUCCESS            1u

#define XHCI_MAX_TRBS         256
#define XHCI_MAX_SLOTS        32
#define XHCI_MAX_SCRATCHPADS  64
#define XHCI_MAX_CONFIG       4096

#define BIT32(n) (1u << (n))
#define BIT64(n) (1ULL << (n))

/* Keep controller-visible objects page/segment aligned. Everything lives in
 * the kernel image, which the native kernel identity maps below 512 GiB. */
typedef struct __attribute__((aligned(64))) {
    uint32_t d[4];
} XHCI_TRB;

typedef struct __attribute__((packed, aligned(64))) {
    uint64_t ring_segment_base;
    uint32_t ring_segment_size;
    uint32_t reserved;
} XHCI_ERST_ENTRY;

static volatile uint8_t *xhci_base;
static volatile uint8_t *xhci_op;
static volatile uint8_t *xhci_runtime;
static volatile uint32_t *xhci_db;
static uint32_t xhci_max_ports;
static uint32_t xhci_ctx_size;
static uint8_t xhci_ready;
static uint8_t xhci_mouse_ready;
static uint8_t xhci_slot;
static uint8_t xhci_ep_id;
static uint8_t xhci_interface;
static uint16_t xhci_ep_mps;
static uint8_t xhci_ep_interval;
static uint8_t xhci_speed;

static XHCI_TRB command_ring[XHCI_MAX_TRBS] __attribute__((aligned(64)));
static XHCI_TRB event_ring[XHCI_MAX_TRBS] __attribute__((aligned(64)));
static XHCI_TRB control_ring[XHCI_MAX_TRBS] __attribute__((aligned(64)));
static XHCI_TRB mouse_ring[XHCI_MAX_TRBS] __attribute__((aligned(64)));
static XHCI_ERST_ENTRY erst[1] __attribute__((aligned(64)));
static uint64_t dcbaa[XHCI_MAX_SLOTS] __attribute__((aligned(64)));
static uint8_t input_context[2048] __attribute__((aligned(64)));
static uint8_t output_context[2048] __attribute__((aligned(64)));
static uint64_t scratchpad_ptrs[XHCI_MAX_SCRATCHPADS] __attribute__((aligned(64)));
static uint8_t scratchpads[XHCI_MAX_SCRATCHPADS][4096] __attribute__((aligned(4096)));
static uint8_t config_data[XHCI_MAX_CONFIG] __attribute__((aligned(65536)));
static uint8_t device_descriptor[18] __attribute__((aligned(64)));
static uint8_t mouse_report[16] __attribute__((aligned(64)));

static size_t command_index;
static uint8_t command_cycle = 1;
static size_t event_index;
static uint8_t event_cycle = 1;
static size_t control_index;
static uint8_t control_cycle = 1;
static size_t mouse_index;
static uint8_t mouse_cycle = 1;

extern void native_pointer_move(int32_t dx, int32_t dy, uint8_t buttons);

static inline uint32_t io_in32(uint16_t port) {
    uint32_t v;
    __asm__ __volatile__("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void io_out32(uint16_t port, uint32_t v) {
    __asm__ __volatile__("outl %0, %1" : : "a"(v), "Nd"(port));
}

static uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    uint32_t address = 0x80000000u |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)dev << 11) |
                       ((uint32_t)fn << 8) |
                       (off & 0xFCu);
    io_out32(PCI_CONFIG_ADDRESS, address);
    return io_in32(PCI_CONFIG_DATA);
}

static void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t value) {
    uint32_t address = 0x80000000u |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)dev << 11) |
                       ((uint32_t)fn << 8) |
                       (off & 0xFCu);
    io_out32(PCI_CONFIG_ADDRESS, address);
    io_out32(PCI_CONFIG_DATA, value);
}

static uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    uint32_t v = pci_config_read32(bus, dev, fn, off);
    return (uint16_t)((v >> ((off & 2u) * 8u)) & 0xFFFFu);
}

static int pci_find_xhci(uint8_t *bus, uint8_t *dev, uint8_t *fn, uint64_t *bar) {
    for (uint32_t b = 0; b < 256; ++b) {
        for (uint32_t d = 0; d < 32; ++d) {
            for (uint32_t f = 0; f < 8; ++f) {
                uint32_t id = pci_config_read32((uint8_t)b, (uint8_t)d, (uint8_t)f, 0x00);
                if ((uint16_t)id == 0xFFFFu)
                    continue;

                uint32_t class_reg = pci_config_read32((uint8_t)b, (uint8_t)d, (uint8_t)f, 0x08);
                uint8_t class_code = (uint8_t)(class_reg >> 24);
                uint8_t subclass = (uint8_t)(class_reg >> 16);
                uint8_t prog_if = (uint8_t)(class_reg >> 8);
                if (class_code != 0x0Cu || subclass != 0x03u || prog_if != 0x30u)
                    continue;

                uint32_t command = pci_config_read32((uint8_t)b, (uint8_t)d, (uint8_t)f, 0x04);
                command |= 0x00000006u; /* memory space + bus master */
                pci_config_write32((uint8_t)b, (uint8_t)d, (uint8_t)f, 0x04, command);

                uint32_t bar_lo = pci_config_read32((uint8_t)b, (uint8_t)d, (uint8_t)f, 0x10);
                if (bar_lo & 1u)
                    continue;

                uint64_t address = (uint64_t)(bar_lo & 0xFFFFFFF0u);
                if (((bar_lo >> 1) & 3u) == 2u) {
                    uint32_t bar_hi = pci_config_read32((uint8_t)b, (uint8_t)d, (uint8_t)f, 0x14);
                    address |= (uint64_t)bar_hi << 32;
                }
                if (!address)
                    continue;

                *bus = (uint8_t)b;
                *dev = (uint8_t)d;
                *fn = (uint8_t)f;
                *bar = address;
                return 1;
            }
        }
    }
    return 0;
}

static inline volatile uint32_t *mmio32(volatile uint8_t *base, uint32_t off) {
    return (volatile uint32_t *)(base + off);
}

static inline uint32_t rd32(volatile uint8_t *base, uint32_t off) {
    return *mmio32(base, off);
}

static inline void wr32(volatile uint8_t *base, uint32_t off, uint32_t v) {
    *mmio32(base, off) = v;
}

static inline uint64_t rd64(volatile uint8_t *base, uint32_t off) {
    volatile uint64_t *p = (volatile uint64_t *)(base + off);
    return *p;
}

static inline void wr64(volatile uint8_t *base, uint32_t off, uint64_t v) {
    volatile uint64_t *p = (volatile uint64_t *)(base + off);
    *p = v;
}

static int timed(uint32_t loops) {
    while (loops--) {
        __asm__ __volatile__("pause");
    }
    return 0;
}

static void ring_init(XHCI_TRB *ring, size_t *index, uint8_t *cycle) {
    for (size_t i = 0; i < XHCI_MAX_TRBS; ++i)
        for (int j = 0; j < 4; ++j)
            ring[i].d[j] = 0;
    ring[XHCI_MAX_TRBS - 1].d[0] = (uint32_t)(uintptr_t)ring;
    ring[XHCI_MAX_TRBS - 1].d[1] = (uint32_t)((uint64_t)(uintptr_t)ring >> 32);
    ring[XHCI_MAX_TRBS - 1].d[3] = (TRB_LINK << 10) | TRB_CYCLE | TRB_ENT;
    *index = 0;
    *cycle = 1;
}

static XHCI_TRB *ring_push(XHCI_TRB *ring, size_t *index, uint8_t *cycle) {
    if (*index >= XHCI_MAX_TRBS - 1) {
        *index = 0;
        *cycle ^= 1u;
    }
    XHCI_TRB *trb = &ring[*index];
    for (int j = 0; j < 4; ++j)
        trb->d[j] = 0;
    ++*index;
    return trb;
}

static void event_ring_init(void) {
    for (size_t i = 0; i < XHCI_MAX_TRBS; ++i)
        for (int j = 0; j < 4; ++j)
            event_ring[i].d[j] = 0;
    event_index = 0;
    event_cycle = 1;
}

static int event_next(XHCI_TRB *out) {
    XHCI_TRB *e = &event_ring[event_index];
    if ((e->d[3] & TRB_CYCLE) != event_cycle)
        return 0;
    *out = *e;
    for (int j = 0; j < 4; ++j)
        e->d[j] = 0;
    ++event_index;
    if (event_index == XHCI_MAX_TRBS) {
        event_index = 0;
        event_cycle ^= 1u;
    }
    wr64(xhci_runtime, XHCI_ERDP, (uint64_t)(uintptr_t)&event_ring[event_index] | BIT64(3));
    return 1;
}

static int wait_command(uint8_t expected_slot, uint32_t timeout) {
    XHCI_TRB event;
    while (timeout--) {
        while (event_next(&event)) {
            uint32_t type = (event.d[3] >> 10) & 0x3Fu;
            if (type != TRB_COMPLETION)
                continue;
            uint32_t cc = (event.d[2] >> 24) & 0xFFu;
            uint8_t slot = (uint8_t)(event.d[3] >> 24);
            if (expected_slot && slot != expected_slot)
                continue;
            return cc == CC_SUCCESS;
        }
        __asm__ __volatile__("pause");
    }
    return 0;
}

static int wait_transfer(uint8_t slot, uint8_t ep, uint32_t timeout, uint32_t *actual) {
    XHCI_TRB event;
    while (timeout--) {
        while (event_next(&event)) {
            uint32_t type = (event.d[3] >> 10) & 0x3Fu;
            if (type != TRB_TRANSFER_EVENT)
                continue;
            uint8_t event_slot = (uint8_t)(event.d[3] >> 24);
            uint8_t event_ep = (uint8_t)((event.d[3] >> 16) & 0x1Fu);
            if (event_slot != slot || event_ep != ep)
                continue;
            uint32_t cc = (event.d[2] >> 24) & 0xFFu;
            if (actual)
                *actual = event.d[2] & 0xFFFFFFu;
            return cc == CC_SUCCESS || cc == 13u;
        }
        __asm__ __volatile__("pause");
    }
    return 0;
}

static int port_reset(uint8_t port) {
    uint32_t off = XHCI_PORTSC_BASE + (uint32_t)(port - 1) * XHCI_PORTSC_STRIDE;
    uint32_t v = rd32(xhci_op, off);
    if (!(v & PORTSC_CCS))
        return 0;
    if (v & PORTSC_PED)
        return 1;

    wr32(xhci_op, off, v | PORTSC_PR);
    for (uint32_t i = 0; i < 2000000; ++i) {
        uint32_t p = rd32(xhci_op, off);
        if (!(p & PORTSC_PR) && (p & PORTSC_PED))
            return 1;
        if (!(p & PORTSC_CCS))
            return 0;
        __asm__ __volatile__("pause");
    }
    return 0;
}

static void legacy_handoff(void) {
    uint32_t hcc = rd32(xhci_base, XHCI_HCCPARAMS1);
    uint16_t ext = (uint16_t)(hcc >> 16);
    if (!ext)
        return;

    volatile uint8_t *p = xhci_base + ((uint32_t)ext << 2);
    for (int n = 0; n < 64 && p < xhci_base + 0x1000; ++n) {
        uint32_t cap = *(volatile uint32_t *)p;
        uint8_t id = (uint8_t)(cap & 0xFFu);
        uint8_t next = (uint8_t)((cap >> 8) & 0xFFu);
        if (id == 1) {
            uint32_t v = *(volatile uint32_t *)p;
            if (v & BIT32(16)) {
                v |= BIT32(24);
                *(volatile uint32_t *)p = v;
                for (uint32_t i = 0; i < 1000000; ++i) {
                    v = *(volatile uint32_t *)p;
                    if (!(v & BIT32(16)))
                        break;
                    __asm__ __volatile__("pause");
                }
            }
            return;
        }
        if (!next)
            break;
        p += (uint32_t)next << 2;
    }
}

static int xhci_controller_start(uint64_t bar) {
    xhci_base = (volatile uint8_t *)(uintptr_t)bar;
    uint8_t caplen = *(volatile uint8_t *)(xhci_base + XHCI_CAPLENGTH);
    uint32_t db_off = rd32(xhci_base, XHCI_DBOFF) & ~3u;
    uint32_t rt_off = rd32(xhci_base, XHCI_RTSOFF) & ~0x1Fu;
    uint32_t hcs1 = rd32(xhci_base, XHCI_HCSPARAMS1);
    uint32_t hcs2 = rd32(xhci_base, XHCI_HCSPARAMS2);
    uint32_t hcc1 = rd32(xhci_base, XHCI_HCCPARAMS1);

    xhci_max_ports = (hcs1 >> 24) & 0xFFu;
    xhci_max_ports = xhci_max_ports > 15 ? 15 : xhci_max_ports;
    xhci_ctx_size = (hcc1 & BIT32(2)) ? 64u : 32u;

    uint32_t cmd = (hcs2 >> 27 & 0x1Fu) << 5 | ((hcs2 >> 21) & 0x1Fu);
    if (cmd > XHCI_MAX_SCRATCHPADS)
        return 0;

    legacy_handoff();

    xhci_op = xhci_base + caplen;
    xhci_runtime = xhci_base + rt_off;
    xhci_db = (volatile uint32_t *)(xhci_base + db_off);

    wr32(xhci_op, XHCI_USBCMD, rd32(xhci_op, XHCI_USBCMD) & ~USB_CMD_RUN);
    for (uint32_t i = 0; i < 1000000; ++i) {
        if (rd32(xhci_op, XHCI_USBSTS) & USB_STS_HCH)
            break;
        __asm__ __volatile__("pause");
    }

    wr32(xhci_op, XHCI_USBCMD, rd32(xhci_op, XHCI_USBCMD) | USB_CMD_HCRST);
    for (uint32_t i = 0; i < 2000000; ++i) {
        uint32_t c = rd32(xhci_op, XHCI_USBCMD);
        uint32_t s = rd32(xhci_op, XHCI_USBSTS);
        if (!(c & USB_CMD_HCRST) && (s & USB_STS_HCH))
            break;
        __asm__ __volatile__("pause");
        if (i + 1 == 2000000)
            return 0;
    }

    wr32(xhci_op, XHCI_PAGESIZE, 1);
    if (!(rd32(xhci_op, XHCI_PAGESIZE) & 1u))
        return 0;

    for (size_t i = 0; i < XHCI_MAX_SLOTS; ++i)
        dcbaa[i] = 0;
    if (cmd) {
        dcbaa[0] = (uint64_t)(uintptr_t)scratchpad_ptrs;
        for (uint32_t i = 0; i < cmd; ++i)
            scratchpad_ptrs[i] = (uint64_t)(uintptr_t)scratchpads[i];
    }

    ring_init(command_ring, &command_index, &command_cycle);
    ring_init(control_ring, &control_index, &control_cycle);
    ring_init(mouse_ring, &mouse_index, &mouse_cycle);
    event_ring_init();

    erst[0].ring_segment_base = (uint64_t)(uintptr_t)event_ring;
    erst[0].ring_segment_size = XHCI_MAX_TRBS;
    erst[0].reserved = 0;

    wr64(xhci_op, XHCI_DCBAAP, (uint64_t)(uintptr_t)dcbaa);
    wr64(xhci_op, XHCI_CRCR, (uint64_t)(uintptr_t)command_ring | 1u);

    wr32(xhci_runtime, XHCI_IMAN, 0);
    wr32(xhci_runtime, XHCI_IMOD, 0);
    wr32(xhci_runtime, XHCI_ERSTSZ, 1);
    wr64(xhci_runtime, XHCI_ERSTBA, (uint64_t)(uintptr_t)erst);
    wr64(xhci_runtime, XHCI_ERDP, (uint64_t)(uintptr_t)event_ring);

    uint32_t max_slots = hcs1 & 0xFFu;
    if (max_slots > XHCI_MAX_SLOTS - 1)
        max_slots = XHCI_MAX_SLOTS - 1;
    wr32(xhci_op, XHCI_CONFIG, max_slots);
    wr32(xhci_op, XHCI_USBCMD, rd32(xhci_op, XHCI_USBCMD) | USB_CMD_RUN);

    for (uint32_t i = 0; i < 1000000; ++i) {
        if (!(rd32(xhci_op, XHCI_USBSTS) & USB_STS_HCH)) {
            xhci_ready = 1;
            return 1;
        }
        __asm__ __volatile__("pause");
    }
    return 0;
}

static void submit_command(uint32_t p0, uint32_t p1, uint32_t p2, uint32_t ctl) {
    XHCI_TRB *trb = ring_push(command_ring, &command_index, &command_cycle);
    trb->d[0] = p0;
    trb->d[1] = p1;
    trb->d[2] = p2;
    trb->d[3] = ctl | (command_cycle ? TRB_CYCLE : 0);
    xhci_db[0] = 0;
}

static int enable_slot(void) {
    submit_command(0, 0, 0, TRB_ENABLE_SLOT << 10);
    XHCI_TRB event;
    for (uint32_t t = 0; t < 2000000; ++t) {
        while (event_next(&event)) {
            uint32_t type = (event.d[3] >> 10) & 0x3Fu;
            if (type != TRB_COMPLETION)
                continue;
            uint32_t cc = (event.d[2] >> 24) & 0xFFu;
            if (cc != CC_SUCCESS)
                return 0;
            xhci_slot = (uint8_t)(event.d[3] >> 24);
            return xhci_slot != 0;
        }
        __asm__ __volatile__("pause");
    }
    return 0;
}

static uint32_t *ctx_at(void *ctx, uint32_t index) {
    return (uint32_t *)((uint8_t *)ctx + (size_t)index * xhci_ctx_size);
}

static int address_device(uint8_t port, uint8_t speed) {
    for (size_t i = 0; i < sizeof(input_context); ++i)
        input_context[i] = 0;
    for (size_t i = 0; i < sizeof(output_context); ++i)
        output_context[i] = 0;

    dcbaa[xhci_slot] = (uint64_t)(uintptr_t)output_context;

    uint32_t *ictx = ctx_at(input_context, 0);
    ictx[0] = BIT32(0) | BIT32(1);

    uint32_t *slot = ctx_at(input_context, 1);
    slot[0] = ((uint32_t)(speed & 0xFu) << 20) | (1u << 27);
    slot[1] = (uint32_t)port << 16;

    uint32_t mps = speed == 1 ? 8u : 64u;
    uint32_t *ep0 = ctx_at(input_context, 2);
    ep0[0] = (3u << 1);
    ep0[1] = (3u << 1) | (4u << 3) | (3u << 1);
    ep0[1] &= ~(0xFFFFu << 16);
    ep0[1] |= mps << 16;
    ep0[2] = ((uint32_t)(uintptr_t)control_ring & 0xFFFFFFF0u) | 1u;
    ep0[3] = (8u & 0xFFFFu);

    submit_command((uint32_t)(uintptr_t)input_context,
                   (uint32_t)((uint64_t)(uintptr_t)input_context >> 32),
                   0, TRB_ADDRESS_DEVICE << 10);
    return wait_command(xhci_slot, 2000000);
}

static void setup_trb(XHCI_TRB *trb, uint8_t bm, uint8_t request,
                      uint16_t value, uint16_t index, uint16_t length) {
    trb->d[0] = (uint32_t)bm | ((uint32_t)request << 8) | ((uint32_t)value << 16);
    trb->d[1] = (uint32_t)index | ((uint32_t)length << 16);
    uint32_t trt = length ? ((bm & 0x80u) ? 3u : 2u) : 0u;
    trb->d[2] = 8u | (trt << 16);
    trb->d[3] = (TRB_SETUP << 10) | TRB_IDT | (control_cycle ? TRB_CYCLE : 0);
}

static int control_transfer(uint8_t bm, uint8_t request, uint16_t value,
                            uint16_t index, void *buffer, uint16_t length) {
    XHCI_TRB *setup = ring_push(control_ring, &control_index, &control_cycle);
    setup_trb(setup, bm, request, value, index, length);

    int data_in = (bm & 0x80u) != 0;
    if (length) {
        XHCI_TRB *data = ring_push(control_ring, &control_index, &control_cycle);
        data->d[0] = (uint32_t)(uintptr_t)buffer;
        data->d[1] = (uint32_t)((uint64_t)(uintptr_t)buffer >> 32);
        data->d[2] = length;
        data->d[3] = (TRB_DATA << 10) | (data_in ? TRB_DIR_IN : 0) | TRB_CHAIN |
                     (control_cycle ? TRB_CYCLE : 0);
    }

    XHCI_TRB *status = ring_push(control_ring, &control_index, &control_cycle);
    status->d[0] = status->d[1] = status->d[2] = 0;
    status->d[3] = (TRB_STATUS << 10) | TRB_IOC |
                   ((!length || !data_in) ? TRB_DIR_IN : 0) |
                   (control_cycle ? TRB_CYCLE : 0);

    xhci_db[xhci_slot] = 1;
    return wait_transfer(xhci_slot, 1, 2000000, NULL);
}

static int parse_hid_mouse(const uint8_t *buf, size_t len,
                           uint8_t *iface, uint8_t *ep_addr,
                           uint16_t *mps, uint8_t *interval,
                           uint8_t *config_value) {
    if (len < 9 || buf[1] != 2)
        return 0;
    *config_value = buf[5];

    size_t p = 0;
    uint8_t current_iface = 0xFF;
    uint8_t current_protocol = 0;
    uint8_t current_subclass = 0;
    while (p + 2 <= len) {
        uint8_t dl = buf[p];
        uint8_t dt = buf[p + 1];
        if (dl < 2 || p + dl > len)
            break;

        if (dt == 4 && dl >= 9) {
            current_iface = buf[p + 2];
            current_subclass = buf[p + 6];
            current_protocol = buf[p + 7];
        } else if (dt == 5 && dl >= 7 && current_iface != 0xFF &&
                   current_subclass == 1 && current_protocol == 2) {
            uint8_t addr = buf[p + 2];
            uint8_t attr = buf[p + 3] & 3u;
            uint16_t packet = (uint16_t)buf[p + 4] | ((uint16_t)buf[p + 5] << 8);
            if ((addr & 0x80u) && attr == 3u) {
                *iface = current_iface;
                *ep_addr = addr;
                *mps = packet & 0x7FFu;
                *interval = buf[p + 6];
                return 1;
            }
        }
        p += dl;
    }
    return 0;
}

static int configure_mouse_endpoint(uint8_t ep_addr, uint16_t mps, uint8_t interval) {
    xhci_ep_id = (uint8_t)(((ep_addr & 0x0Fu) << 1) | ((ep_addr & 0x80u) ? 0 : 1));
    xhci_ep_mps = mps ? mps : 8;
    xhci_ep_interval = interval ? interval : 1;

    size_t ctx_ep_index = (size_t)xhci_ep_id + 1;
    if (ctx_ep_index >= sizeof(input_context) / 32)
        return 0;

    uint32_t *ictx = ctx_at(input_context, 0);
    ictx[0] = BIT32(0) | BIT32(xhci_ep_id + 1);

    uint32_t *slot = ctx_at(input_context, 1);
    slot[0] = ((uint32_t)xhci_speed << 20) | ((uint32_t)ctx_ep_index << 27);

    uint32_t *ep0 = ctx_at(input_context, 2);
    ep0[0] = 0;
    ep0[1] = ((uint32_t)xhci_ep_mps << 16) | (3u << 1) | (4u << 3);
    ep0[2] = ((uint32_t)(uintptr_t)control_ring & 0xFFFFFFF0u) | 1u;
    ep0[3] = 8u;

    uint32_t interval_field = 1;
    if (xhci_speed >= 3) {
        interval_field = xhci_ep_interval > 16 ? 16 : xhci_ep_interval;
    } else {
        uint32_t period = xhci_ep_interval;
        interval_field = 3;
        while (period > 1 && interval_field < 16) {
            period = (period + 1) >> 1;
            ++interval_field;
        }
    }

    uint32_t *ep = ctx_at(input_context, ctx_ep_index);
    ep[0] = (interval_field & 0xFFu) << 16;
    ep[1] = ((uint32_t)xhci_ep_mps << 16) | (3u << 1) | (7u << 3);
    ep[2] = ((uint32_t)(uintptr_t)mouse_ring & 0xFFFFFFF0u) | 1u;
    ep[3] = (uint32_t)xhci_ep_mps;

    submit_command((uint32_t)(uintptr_t)input_context,
                   (uint32_t)((uint64_t)(uintptr_t)input_context >> 32),
                   0, TRB_CONFIGURE_EP << 10);
    return wait_command(xhci_slot, 2000000);
}

static int submit_mouse_transfer(void) {
    XHCI_TRB *trb = ring_push(mouse_ring, &mouse_index, &mouse_cycle);
    trb->d[0] = (uint32_t)(uintptr_t)mouse_report;
    trb->d[1] = (uint32_t)((uint64_t)(uintptr_t)mouse_report >> 32);
    trb->d[2] = xhci_ep_mps;
    trb->d[3] = (TRB_NORMAL << 10) | TRB_IOC | (mouse_cycle ? TRB_CYCLE : 0);
    xhci_db[xhci_slot] = xhci_ep_id;
    return 1;
}

static int usb_mouse_setup(void) {
    for (uint32_t port = 1; port <= xhci_max_ports; ++port) {
        uint32_t psc = rd32(xhci_op, XHCI_PORTSC_BASE + (port - 1) * XHCI_PORTSC_STRIDE);
        if (!(psc & PORTSC_CCS))
            continue;
        if (!port_reset((uint8_t)port))
            continue;

        xhci_speed = (uint8_t)((rd32(xhci_op, XHCI_PORTSC_BASE + (port - 1) * XHCI_PORTSC_STRIDE) >> PORTSC_SPEED_SHIFT) & PORTSC_SPEED_MASK);
        if (!xhc​​i_speed)
            continue;
        if (xhc​​i_speed > 4)
            continue;

        if (!enable_slot())
            continue;
        if (!address_device((uint8_t)port, xhci_speed))
            continue;

        for (size_t i = 0; i < sizeof(device_descriptor); ++i)
            device_descriptor[i] = 0;
        if (!control_transfer(0x80, 6, 0x0100, 0, device_descriptor, sizeof(device_descriptor)))
            continue;

        for (size_t i = 0; i < 9; ++i)
            config_data[i] = 0;
        if (!control_transfer(0x80, 6, 0x0200, 0, config_data, 9))
            continue;
        uint16_t total = (uint16_t)config_data[2] | ((uint16_t)config_data[3] << 8);
        if (total < 9) total = 9;
        if (total > XHCI_MAX_CONFIG) total = XHCI_MAX_CONFIG;
        if (!control_transfer(0x80, 6, 0x0200, 0, config_data, total))
            continue;

        uint8_t iface = 0, ep_addr = 0, interval = 0, config_value = 0;
        uint16_t mps = 0;
        if (!parse_hid_mouse(config_data, total, &iface, &ep_addr, &mps, &interval, &config_value))
            continue;

        if (!control_transfer(0x00, 9, config_value, 0, NULL, 0))
            continue;
        xhci_interface = iface;
        if (!control_transfer(0x21, 11, 0, xhci_interface, NULL, 0))
            continue;
        if (!configure_mouse_endpoint(ep_addr, mps, interval))
            continue;

        xhci_mouse_ready = 1;
        for (size_t i = 0; i < sizeof(mouse_report); ++i)
            mouse_report[i] = 0;
        submit_mouse_transfer();
        return 1;
    }
    return 0;
}

int native_usb_init(void) {
    uint8_t bus = 0, dev = 0, fn = 0;
    uint64_t bar = 0;
    xhci_ready = 0;
    xhci_mouse_ready = 0;
    if (!pci_find_xhci(&bus, &dev, &fn, &bar))
        return 0;
    (void)bus;
    (void)dev;
    (void)fn;
    if (!xhci_controller_start(bar))
        return 0;
    return usb_mouse_setup();
}

void native_usb_poll(void) {
    if (!xhci_mouse_ready)
        return;

    XHCI_TRB event;
    while (event_next(&event)) {
        uint32_t type = (event.d[3] >> 10) & 0x3Fu;
        if (type != TRB_TRANSFER_EVENT)
            continue;
        uint8_t slot = (uint8_t)(event.d[3] >> 24);
        uint8_t ep = (uint8_t)((event.d[3] >> 16) & 0x1Fu);
        if (slot != xhci_slot || ep != xhci_ep_id)
            continue;
        uint32_t cc = (event.d[2] >> 24) & 0xFFu;
        if (cc == CC_SUCCESS || cc == 13u) {
            uint8_t buttons = mouse_report[0];
            int32_t dx = (int8_t)mouse_report[1];
            int32_t dy = (int8_t)mouse_report[2];
            if (dx || dy || buttons)
                native_pointer_move(dx, -dy, buttons);
        }
        submit_mouse_transfer();
    }
}

int native_usb_mouse_present(void) {
    return xhci_mouse_ready != 0;
}
