#include "platform_i2c.h"
#include "stm32f4xx_hal.h"

extern I2C_HandleTypeDef hi2c1;

bool Platform_I2C_ReadReg(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len) {
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(&hi2c1, dev_addr, reg_addr, 1, data, len, 100);
    return (status == HAL_OK);
}

bool Platform_I2C_WriteReg(uint8_t dev_addr, uint8_t reg_addr, const uint8_t *data, uint16_t len) {
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(&hi2c1, dev_addr, reg_addr, 1, (uint8_t *)data, len, 100);
    return (status == HAL_OK);
}

bool Platform_I2C_IsDeviceReady(uint8_t dev_addr, uint32_t trials, uint32_t timeout_ms) {
    return (HAL_I2C_IsDeviceReady(&hi2c1, dev_addr, trials, timeout_ms) == HAL_OK);
}
