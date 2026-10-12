#ifndef PLATFORM_I2C_H
#define PLATFORM_I2C_H

#include <stdint.h>
#include <stdbool.h>

bool Platform_I2C_ReadReg(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);

#endif