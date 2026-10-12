#include "platform_spi.h"
#include "stm32f4xx_hal.h"

extern SPI_HandleTypeDef hspi2;

void Platform_SPI_SetCS(bool active) {
    if (active) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
    }
}

bool Platform_SPI_TransmitReceive(uint8_t *tx_data, uint8_t *rx_data, uint16_t len) {
    HAL_StatusTypeDef status;
    
    if (rx_data == NULL && tx_data != NULL) {
        status = HAL_SPI_Transmit(&hspi2, tx_data, len, 1000);
    } 
    else if (tx_data == NULL && rx_data != NULL) {
        status = HAL_SPI_Receive(&hspi2, rx_data, len, 1000);
    } 
    else {
        status = HAL_SPI_TransmitReceive(&hspi2, tx_data, rx_data, len, 1000);
    }
    
    return (status == HAL_OK);
}