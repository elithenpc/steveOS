#include <stdint.h>
#include "i2c.h"

extern uint8_t native_keyboard_read_scancode_arch(void);
extern void native_pointer_move(int32_t dx, int32_t dy, uint8_t buttons);

static uint8_t i2c_started;
static uint8_t cursor_seeded;

uint8_t native_keyboard_read_scancode(void) {
    if (!i2c_started) {
        i2c_started = 1;
        if (native_i2c_hid_init()) {
            /* arch.c already owns the framebuffer cursor. Toggle it twice
             * through the public movement primitive to seed a visible cursor
             * even when the machine has no PS/2 or USB mouse. */
            if (!cursor_seeded) {
                native_pointer_move(1, 0, 0);
                native_pointer_move(-1, 0, 0);
                cursor_seeded = 1;
            }
        }
    }

    native_i2c_hid_poll();
    return native_keyboard_read_scancode_arch();
}
