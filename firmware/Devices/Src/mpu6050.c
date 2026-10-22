#include "mpu6050.h"
#include "platform_i2c.h"
#include <stdint.h>
#include <stdio.h>

bool MPU6050_Init(void) {
    uint8_t chip_id = 0;

    bool ok = Platform_I2C_ReadReg(MPU6050_I2C_ADDR, MPU6050_REG_WHO_AM_I, &chip_id, 1);
    if (ok) {
        printf("[MPU6050] WHO_AM_I: 0x%02X ", chip_id);
        if (chip_id == 0x68) {
            printf("(OK)\r\n");

            /* Вихід з режиму Sleep (запис 0 у PWR_MGMT_1) */
            uint8_t pwr_mgmt = 0x00;
            if (Platform_I2C_WriteReg(MPU6050_I2C_ADDR, MPU6050_REG_PWR_MGMT_1, &pwr_mgmt, 1)) {
                printf("[MPU6050] Прокинуто, активовано робочий режим.\r\n");
                return true;
            }
            printf("[MPU6050] Помилка запису PWR_MGMT_1, датчик лишився в режимі сну!\r\n");
        } else {
            printf("(Невірний ID, очікується 0x68)\r\n");
        }
    } else {
        printf("[MPU6050] Помилка зчитування по I2C!\r\n");
    }
    return false;
}

bool MPU6050_ReadTemperature(float *temp_c) {
    uint8_t raw_data[2] = {0};

    /* Зчитуємо послідовно 2 байти (TEMP_OUT_H та TEMP_OUT_L) */
    bool ok = Platform_I2C_ReadReg(MPU6050_I2C_ADDR, MPU6050_REG_TEMP_OUT_H, raw_data, 2);

    if (ok) {
        /* Об'єднуємо два байти в одне 16-бітне знакове число */
        int16_t raw_temp = (int16_t)((raw_data[0] << 8) | raw_data[1]);

        /* Розрахунок температури згідно з даташитом */

        *temp_c = ((float)raw_temp / 340.0f) + 36.53f;
    }

    return ok;
}
