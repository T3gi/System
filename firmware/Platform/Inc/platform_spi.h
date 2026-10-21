#ifndef PLATFORM_SPI_H
#define PLATFORM_SPI_H

#include <stdint.h>
#include <stdbool.h>

void Platform_SPI_SetCS(bool active);

/* HAL path (hspi2 as configured by CubeMX). */
bool Platform_SPI_TransmitReceive(uint8_t *tx_data, uint8_t *rx_data, uint16_t len);

/*
 * Register path, used by the W25Q64 driver: SPI2 master, mode 0, 8-bit,
 * MSB first, software CS on PB12.  br is the baud-rate index:
 * clock = APB1 (42 MHz) / 2^(br+1), so br=0 -> 21 MHz and br=4 -> 1.3 MHz.
 */
void Platform_SPI_RegInit(uint32_t br);

void Platform_SPI_SetSpeed(uint32_t br);

/* One full-duplex byte; returns 0xFF if the peripheral does not respond. */
uint8_t Platform_SPI_Xfer(uint8_t v);

#endif
