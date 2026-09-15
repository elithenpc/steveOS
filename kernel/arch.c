#include <stdint.h>
#include "../src/bootinfo.h"

extern void native_default_isr(void);

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} GDTR;

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} IDT_GATE;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} IDTR;

static uint64_t gdt[3] __attribute__((aligned(8))) = {
    0x0000000000000000ULL,
    0x00AF9A000000FFFFULL,
    0x00AF92000000FFFFULL
};

static IDT_GATE idt[256] __attribute__((aligned(16)));
static uint64_t page_tables[1024] __attribute__((aligned(4096))) = {0};

static void load_gdt(void) {
    GDTR gdtr = { (uint16_t)(sizeof(gdt) - 1), (uint64_t)gdt };
    __asm__ __volatile__("lgdt %0" : : "m"(gdtr));
    __asm__ __volatile__(
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        : : : "rax", "memory");
}

void native_gdt_init(void) {
    load_gdt();
}

static void idt_gate_set(IDT_GATE *g, uintptr_t address) {
    g->offset_low = (uint16_t)(address & 0xFFFF);
    g->selector = 0x08;
    g->ist = 0;
    g->type_attr = 0x8E;
    g->offset_mid = (uint16_t)((address >> 16) & 0xFFFF);
    g->offset_high = (uint32_t)(address >> 32);
    g->zero = 0;
}

void native_idt_init(void) {
    uintptr_t handler = (uintptr_t)&native_default_isr;
    for (int i = 0; i < 256; ++i)
        idt_gate_set(&idt[i], handler);

    IDTR idtr = { (uint16_t)(sizeof(idt) - 1), (uint64_t)idt };
    __asm__ __volatile__("lidt %0" : : "m"(idtr));
}

void native_paging_init(const STEVEOS_BOOT_INFO *boot) {
    for (int i = 0; i < 512; ++i) {
        page_tables[i] = 0;
        page_tables[512 + i] = ((uint64_t)i << 30) | 0x83ULL;
    }

    uintptr_t pml4 = (uintptr_t)&page_tables[0];
    uintptr_t pdpt = (uintptr_t)&page_tables[512];
    if (boot) {
        uintptr_t image_base = (uintptr_t)boot->kernel_base;
        uintptr_t local_base = (uintptr_t)&page_tables[0];
        pml4 = image_base + (local_base - image_base);
        pdpt = pml4 + ((uintptr_t)&page_tables[512] - local_base);
    }

    page_tables[0] = (uint64_t)pdpt | 0x03ULL;
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(pml4) : "memory");
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void io_wait(void) {
    outb(0x80, 0);
}

static int wait_input_clear(void) {
    for (uint32_t i = 0; i < 100000; ++i) {
        if (!(inb(0x64) & 2)) return 1;
        io_wait();
    }
    return 0;
}

static int wait_output_full(void) {
    for (uint32_t i = 0; i < 100000; ++i) {
        if (inb(0x64) & 1) return 1;
        io_wait();
    }
    return 0;
}

static int ps2_read_byte(uint8_t *value) {
    if (!wait_output_full()) return 0;
    *value = inb(0x60);
    return 1;
}

static int ps2_send_mouse(uint8_t command) {
    if (!wait_input_clear()) return 0;
    outb(0x64, 0xD4);
    if (!wait_input_clear()) return 0;
    outb(0x60, command);
    uint8_t ack = 0;
    return ps2_read_byte(&ack) && ack == 0xFA;
}

int native_mouse_init(void) {
    uint8_t status;
    if (!wait_input_clear()) return 0;
    outb(0x64, 0xA8);

    if (!wait_input_clear()) return 0;
    outb(0x64, 0x20);
    if (!ps2_read_byte(&status)) return 0;

    status |= 0x02;
    status &= (uint8_t)~0x20;
    if (!wait_input_clear()) return 0;
    outb(0x64, 0x60);
    if (!wait_input_clear()) return 0;
    outb(0x60, status);

    ps2_send_mouse(0xF6);
    ps2_send_mouse(0xF4);
    return 1;
}

int native_mouse_read_packet(int8_t *dx, int8_t *dy, uint8_t *buttons) {
    static uint8_t packet[3];
    static uint8_t index;
    static uint8_t button_state;

    while (inb(0x64) & 1) {
        uint8_t status = inb(0x64);
        uint8_t value = inb(0x60);
        if (!(status & 0x20))
            continue;

        if (index == 0) {
            if (!(value & 0x08))
                continue;
            packet[0] = value;
            index = 1;
            continue;
        }

        packet[index++] = value;
        if (index < 3)
            continue;

        index = 0;
        if ((packet[0] & 0xC0) != 0)
            continue;

        button_state = packet[0] & 0x07;
        *dx = (int8_t)packet[1];
        *dy = -(int8_t)packet[2];
        *buttons = button_state;
        return 1;
    }
    return 0;
}

uint8_t native_keyboard_read_scancode(void) {
    while (inb(0x64) & 1) {
        uint8_t status = inb(0x64);
        uint8_t value = inb(0x60);
        if (!(status & 0x20))
            return value;
    }
    return 0;
}

void native_reboot(void) {
    for (uint32_t i = 0; i < 100000; ++i) {
        if (!(inb(0x64) & 2)) {
            outb(0x64, 0xFE);
            break;
        }
    }
    for (;;) __asm__ __volatile__("cli; hlt");
}

void native_halt(void) {
    for (;;) __asm__ __volatile__("cli; hlt");
}
