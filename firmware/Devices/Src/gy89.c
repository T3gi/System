#include "gy89.h"
#include "platform_i2c.h"
#include <stdint.h>
#include <stdio.h>

/* Read one ID register, print it and compare it with the datasheet value.
 * A NACKed read prints an error instead of a stale value. */
static bool check_id(const char *label, uint8_t addr, uint8_t reg, uint8_t expected)
{
    uint8_t id = 0;

    if (!Platform_I2C_ReadReg(addr, reg, &id, 1)) {
        printf("%s Помилка зчитування по I2C!\r\n", label);
        return false;
    }
    printf("%s 0x%02X ", label, id);
    if (id != expected) {
        printf("(Невірний ID, очікується 0x%02X)\r\n", expected);
        return false;
    }
    printf("(OK)\r\n");
    return true;
}

bool GY89_ReadIDs(void) {
    bool ok = true;

    ok &= check_id("[LSM303D] WHO_AM_I:", LSM303D_ADDR, 0x0F, LSM303D_ID);
    ok &= check_id("[L3GD20]  WHO_AM_I:", L3GD20_ADDR,  0x0F, L3GD20_ID);
    ok &= check_id("[BMP180]  CHIP_ID: ", BMP180_ADDR,  0xD0, BMP180_ID);
    return ok;
}
