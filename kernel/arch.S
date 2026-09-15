.section .text
.global native_default_isr
native_default_isr:
    cli
1:
    hlt
    jmp 1b
