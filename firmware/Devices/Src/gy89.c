#include "gy89.h"
#include "platform_i2c.h"
#include <stdint.h>
#include <stdio.h>

void GY89_ReadIDs(void) {
    uint8_t id = 0;

    Platform_I2C_ReadReg(LSM303D_ADDR, 0x0F, &id, 1);
    printf("[LSM303D] WHO_AM_I: 0x%02X\r\n", id);

    Platform_I2C_ReadReg(L3GD20_ADDR, 0x0F, &id, 1);
    printf("[L3GD20]  WHO_AM_I: 0x%02X\r\n", id);

    Platform_I2C_ReadReg(BMP180_ADDR, 0xD0, &id, 1);
    printf("[BMP180]  CHIP_ID:  0x%02X\r\n", id);
}
