#ifndef STEVEOS_NATIVE_USB_H
#define STEVEOS_NATIVE_USB_H

#include <stdint.h>

int native_usb_init(void);
void native_usb_poll(void);
int native_usb_mouse_present(void);

#endif
