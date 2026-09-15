.section .text
.global _start
.extern native_gdt_init
.extern native_paging_init
.extern native_idt_init
.extern native_input_bind
.extern kernel_main

_start:
    cli
    mov %rdi, %r12
    mov %rsi, %rsp
    andq $-16, %rsp

    call native_gdt_init
    mov %r12, %rdi
    call native_paging_init
    call native_idt_init

    mov %r12, %rdi
    call native_input_bind

    mov %r12, %rdi
    call kernel_main

1:
    cli
    hlt
    jmp 1b
