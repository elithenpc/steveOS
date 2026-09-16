#include <stdint.h>
#include "i2c.h"
#include "usb.h"

extern uint8_t native_keyboard_read_scancode_arch(void);
extern void native_pointer_move(int32_t dx, int32_t dy, uint8_t buttons);

static uint8_t i2c_started;
static uint8_t cursor_seeded;

uint8_t native_keyboard_read_scancode(void) {
    /* A USB receiver mouse is authoritative. Do not let the built-in Dell
     * touchpad inject a second stream of movement at the same time. */
    if (native_usb_mouse_present())
        return native_keyboard_read_scancode_arch();

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
    return native_keyboard_read_scancode_arch();
}
