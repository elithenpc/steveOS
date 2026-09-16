#ifndef STEVEOS_NATIVE_I2C_H
#define STEVEOS_NATIVE_I2C_H

#include <stdint.h>

int native_i2c_hid_init(void);
void native_i2c_hid_poll(void);
int native_i2c_hid_present(void);

#endif
