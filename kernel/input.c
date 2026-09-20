#include <stdint.h>
#include "i2c.h"
#include "usb.h"

extern uint8_t native_keyboard_read_scancode_arch(void);
extern void native_pointer_move(int32_t dx, int32_t dy, uint8_t buttons);

static uint8_t i2c_started;
static uint8_t cursor_seeded;
static uint8_t ps2_keyboard_started;

static inline uint8_t io_inb(uint16_t p){uint8_t v;__asm__ __volatile__("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline void io_outb(uint16_t p,uint8_t v){__asm__ __volatile__("outb %0,%1"::"a"(v),"Nd"(p));}
static void io_wait(void){io_outb(0x80,0);}
static int ps2_wait_input(void){for(uint32_t i=0;i<100000;i++){if(!(io_inb(0x64)&2))return 1;io_wait();}return 0;}
static int ps2_wait_output(void){for(uint32_t i=0;i<100000;i++){if(io_inb(0x64)&1)return 1;io_wait();}return 0;}
static int ps2_read(uint8_t*out){if(!ps2_wait_output())return 0;*out=io_inb(0x60);return 1;}
static int ps2_write_keyboard(uint8_t v){if(!ps2_wait_input())return 0;io_outb(0x60,v);uint8_t a=0;if(!ps2_read(&a))return 0;return a==0xFA;}

static void ps2_keyboard_init(void){
    if(ps2_keyboard_started)return;
    ps2_keyboard_started=1;
    if(!ps2_wait_input())return;
    io_outb(0x64,0xAD);
    if(!ps2_wait_input())return;
    io_outb(0x64,0x20);
    uint8_t cfg=0;
    if(!ps2_read(&cfg))return;
    cfg|=0x01u;
    cfg&=(uint8_t)~0x10u;
    if(!ps2_wait_input())return;
    io_outb(0x64,0x60);
    if(!ps2_wait_input())return;
    io_outb(0x60,cfg);
    ps2_write_keyboard(0xFF);
    for(uint32_t i=0;i<100000;i++){
        if(io_inb(0x64)&1){uint8_t v=io_inb(0x60);if(v==0xAA)break;}
        io_wait();
    }
    ps2_write_keyboard(0xF0);
    ps2_write_keyboard(0x01);
    ps2_write_keyboard(0xF4);
    if(ps2_wait_input())io_outb(0x64,0xAE);
}

uint8_t native_keyboard_read_scancode(void) {
    ps2_keyboard_init();

    /* Do not disable the laptop's I2C HID touchpad just because an xHCI
       controller or USB mouse exists. Both input paths can coexist. */
    if (!i2c_started) {
        i2c_started = 1;
        if (native_i2c_hid_init()) {
            if (!cursor_seeded) {
                native_pointer_move(1, 0, 0);
                native_pointer_move(-1, 0, 0);
                cursor_seeded = 1;
            }
        }
    }

    native_i2c_hid_poll();
    native_usb_poll();
    return native_keyboard_read_scancode_arch();
}
