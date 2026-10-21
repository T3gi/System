#include "platform_spi.h"
#include "stm32f4xx_hal.h"

extern SPI_HandleTypeDef hspi2;

/* CS# on PB12, driven by hand: hardware NSS in master mode deasserts between
 * frames, which would abort a multi-byte flash transaction. */
#define SPI_CS_PORT  GPIOB
#define SPI_CS_PIN   GPIO_PIN_12

void Platform_SPI_SetCS(bool active) {
    if (active) {
        SPI_CS_PORT->BSRR = (uint32_t)SPI_CS_PIN << 16;   /* reset: CS low */
    } else {
        SPI_CS_PORT->BSRR = (uint32_t)SPI_CS_PIN;         /* set: CS high */
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

/* --- Register path ------------------------------------------------------- */

/* Bounded so a missing or mis-wired chip reports an error instead of hanging
 * the firmware — the same lesson the camera polling loops taught. */
#define SPI_SPIN_GUARD  100000u

/**
 * @brief Compose CR1 for master mode 0, 8-bit, MSB first.
 *
 * SSM|SSI are mandatory: with software slave management the master must drive
 * its internal NSS high, otherwise a low NSS raises MODF and the peripheral
 * silently drops out of master mode.
 */
static uint32_t spi_cr1(uint32_t br)
{
    return SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI
           | ((br & 7u) << SPI_CR1_BR_Pos) | SPI_CR1_SPE;
}

void Platform_SPI_RegInit(uint32_t br)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    /* CS# first, and already HIGH, so the chip never sees a partial frame
     * while the SCK/MOSI pins are still being switched to their AF. */
    Platform_SPI_SetCS(false);
    GPIO_InitTypeDef g = {0};
    g.Pin   = SPI_CS_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(SPI_CS_PORT, &g);
    Platform_SPI_SetCS(false);

    g.Pin       = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOB, &g);

    /* Mode 0 (CPOL=0, CPHA=0) — W25Q64 supports modes 0 and 3. */
    SPI2->CR1 = 0;
    SPI2->CR2 = 0;
    SPI2->CR1 = spi_cr1(br);
}

void Platform_SPI_SetSpeed(uint32_t br)
{
    SPI2->CR1 &= ~SPI_CR1_SPE;
    SPI2->CR1 = spi_cr1(br);
}

uint8_t Platform_SPI_Xfer(uint8_t v)
{
    uint32_t guard = SPI_SPIN_GUARD;
    while ((SPI2->SR & SPI_SR_TXE) == 0) { if (--guard == 0) return 0xFF; }
    *(volatile uint8_t *)&SPI2->DR = v;
    guard = SPI_SPIN_GUARD;
    while ((SPI2->SR & SPI_SR_RXNE) == 0) { if (--guard == 0) return 0xFF; }
    return *(volatile uint8_t *)&SPI2->DR;
}
