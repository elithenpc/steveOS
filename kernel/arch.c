#include <stdint.h>

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

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t native_keyboard_read_scancode(void) {
    if (!(inb(0x64) & 1))
        return 0;
    return inb(0x60);
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
