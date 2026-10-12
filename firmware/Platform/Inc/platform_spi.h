#ifndef PLATFORM_SPI_H
#define PLATFORM_SPI_H

#include <stdint.h>
#include <stdbool.h>

void Platform_SPI_SetCS(bool active);

bool Platform_SPI_TransmitReceive(uint8_t *tx_data, uint8_t *rx_data, uint16_t len);

#endif