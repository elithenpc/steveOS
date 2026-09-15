.section .text
.global _start
.extern native_gdt_init
.extern native_idt_init
.extern kernel_main

_start:
    cli
    mov %rsi, %rsp
    andq $-16, %rsp
    call native_gdt_init
    call native_idt_init
    call kernel_main

1:
    cli
    hlt
    jmp 1b
