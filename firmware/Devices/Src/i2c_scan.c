#include "i2c_scan.h"
#include "platform_i2c.h"
#include <stdint.h>
#include <stdio.h>

void I2C_Scan(void) {
    printf("Сканування шини I2C1...\r\n");
    uint8_t count = 0;
    for (uint16_t addr = 1; addr < 128; addr++) {
        if (Platform_I2C_IsDeviceReady((uint8_t)(addr << 1), 1, 10)) {
            printf(" Знайдено пристрій за адресою: 0x%02X\r\n", addr);
            count++;
        }
    }
    printf("Сканування завершено. Знайдено пристроїв: %d\r\n", count);
}
