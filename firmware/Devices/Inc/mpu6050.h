#ifndef MPU6050_H
#define MPU6050_H

#include <stdbool.h>

#define MPU6050_I2C_ADDR        (0x68 << 1)
#define MPU6050_REG_TEMP_OUT_H  0x41
#define MPU6050_REG_PWR_MGMT_1  0x6B
#define MPU6050_REG_WHO_AM_I    0x75

/* Check WHO_AM_I and wake the chip from sleep (PWR_MGMT_1 = 0). */
void MPU6050_Init(void);

/* Read TEMP_OUT_H/L and convert: T = raw / 340 + 36.53.  true on success. */
bool MPU6050_ReadTemperature(float *temp_c);

#endif
