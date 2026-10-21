#ifndef PLATFORM_I2C_H
#define PLATFORM_I2C_H

#include <stdint.h>
#include <stdbool.h>

/* dev_addr is the 8-bit (shifted) address, e.g. 0x68 << 1. */
bool Platform_I2C_ReadReg(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);

bool Platform_I2C_WriteReg(uint8_t dev_addr, uint8_t reg_addr, const uint8_t *data, uint16_t len);

bool Platform_I2C_IsDeviceReady(uint8_t dev_addr, uint32_t trials, uint32_t timeout_ms);

#endif
