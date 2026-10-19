/**
 ******************************************************************************
 * @file    w25q64.c
 * @brief   W25Q64 SPI NOR flash driver on SPI2 (see w25q64.h for wiring).
 *
 * SPI2 is configured with direct register writes rather than HAL_SPI: this HAL
 * subset does not vendor stm32f4xx_hal_spi.c, and per-byte HAL calls would add
 * microseconds of overhead each — which matters when a 256-byte page has to be
 * pushed inside a horizontal blanking gap during frame capture.  The rest of
 * this project already talks to hardware this way (bit-banged SCCB, polled
 * GPIO capture).
 ******************************************************************************
 */
#include "w25q64.h"

/* --- Instruction set ----------------------------------------------------- */
#define CMD_WRITE_ENABLE   0x06u
#define CMD_WRITE_DISABLE  0x04u
#define CMD_READ_STATUS1   0x05u
#define CMD_PAGE_PROGRAM   0x02u
#define CMD_SECTOR_ERASE   0x20u
#define CMD_BLOCK_ERASE64  0xD8u
#define CMD_READ_DATA      0x03u
#define CMD_FAST_READ      0x0Bu
#define CMD_JEDEC_ID       0x9Fu
#define CMD_RELEASE_PD     0xABu

/* CS# on PB12, driven by hand: hardware NSS in master mode deasserts between
 * frames, which would abort a multi-byte flash transaction. */
#define W25_CS_PORT        GPIOB
#define W25_CS_PIN         GPIO_PIN_12

static inline void cs_low(void)
{
    W25_CS_PORT->BSRR = (uint32_t)W25_CS_PIN << 16;   /* reset */
}

static inline void cs_high(void)
{
    W25_CS_PORT->BSRR = (uint32_t)W25_CS_PIN;          /* set */
}

/* Bounded so a missing or mis-wired chip reports an error instead of hanging
 * the firmware — the same lesson the camera polling loops taught. */
#define SPI_SPIN_GUARD  100000u

static uint8_t spi_xfer(uint8_t v)
{
    uint32_t guard = SPI_SPIN_GUARD;
    while ((SPI2->SR & SPI_SR_TXE) == 0) { if (--guard == 0) return 0xFF; }
    *(volatile uint8_t *)&SPI2->DR = v;
    guard = SPI_SPIN_GUARD;
    while ((SPI2->SR & SPI_SR_RXNE) == 0) { if (--guard == 0) return 0xFF; }
    return *(volatile uint8_t *)&SPI2->DR;
}

static void spi_write(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) (void)spi_xfer(p[i]);
}

static void spi_read(uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) p[i] = spi_xfer(0xFF);
}

/** Send a command followed by a 24-bit big-endian address. */
static void cmd_addr(uint8_t cmd, uint32_t addr)
{
    uint8_t hdr[4] = { cmd,
                       (uint8_t)(addr >> 16),
                       (uint8_t)(addr >> 8),
                       (uint8_t)addr };
    spi_write(hdr, 4);
}

/* --- Init ---------------------------------------------------------------- */

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

void w25_set_speed(uint32_t br)
{
    SPI2->CR1 &= ~SPI_CR1_SPE;
    SPI2->CR1 = spi_cr1(br);
}

int w25_init(uint32_t br)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    /* CS# first, and already HIGH, so the chip never sees a partial frame
     * while the SCK/MOSI pins are still being switched to their AF. */
    cs_high();
    GPIO_InitTypeDef g = {0};
    g.Pin   = W25_CS_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(W25_CS_PORT, &g);
    cs_high();

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

    /* A chip left in power-down by a previous run answers 0xFF to everything. */
    cs_low();
    (void)spi_xfer(CMD_RELEASE_PD);
    cs_high();
    HAL_Delay(1);

    uint8_t mfg = 0, type = 0, cap = 0;
    if (w25_read_id(&mfg, &type, &cap) != 0) return -2;
    if (mfg != W25_JEDEC_MFG) return -3;
    return 0;
}

/* --- Primitives ---------------------------------------------------------- */

int w25_read_id(uint8_t *mfg, uint8_t *type, uint8_t *cap)
{
    uint8_t id[3];
    cs_low();
    (void)spi_xfer(CMD_JEDEC_ID);
    spi_read(id, 3);
    cs_high();
    if (mfg)  *mfg  = id[0];
    if (type) *type = id[1];
    if (cap)  *cap  = id[2];
    /* All-ones or all-zeros means nothing is answering (open MISO or no power). */
    if ((id[0] == 0xFF && id[1] == 0xFF && id[2] == 0xFF) ||
        (id[0] == 0x00 && id[1] == 0x00 && id[2] == 0x00)) return -1;
    return 0;
}

uint8_t w25_read_status(void)
{
    cs_low();
    (void)spi_xfer(CMD_READ_STATUS1);
    uint8_t s = spi_xfer(0xFF);
    cs_high();
    return s;
}

int w25_wait_busy(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    for (;;) {
        if ((w25_read_status() & W25_SR1_BUSY) == 0) return 0;
        if (HAL_GetTick() - t0 > timeout_ms) return -1;
    }
}

static void write_enable(void)
{
    cs_low();
    (void)spi_xfer(CMD_WRITE_ENABLE);
    cs_high();
}

int w25_page_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (len == 0 || len > W25_PAGE_SIZE) return -1;
    /* The chip wraps to the start of the page instead of continuing, so a
     * crossing write would silently corrupt the beginning of the page. */
    if ((addr & (W25_PAGE_SIZE - 1u)) + len > W25_PAGE_SIZE) return -2;
    if (w25_wait_busy(1000) != 0) return -3;

    write_enable();
    if ((w25_read_status() & W25_SR1_WEL) == 0) return -4;   /* WP# tied low? */

    cs_low();
    cmd_addr(CMD_PAGE_PROGRAM, addr);
    spi_write(data, len);
    cs_high();

    /* Page program is 0.7 ms typical but up to 3 ms. */
    return (w25_wait_busy(50) == 0) ? 0 : -5;
}

static int erase_generic(uint8_t cmd, uint32_t addr, uint32_t timeout_ms)
{
    if (w25_wait_busy(2000) != 0) return -1;
    write_enable();
    if ((w25_read_status() & W25_SR1_WEL) == 0) return -2;
    cs_low();
    cmd_addr(cmd, addr);
    cs_high();
    return (w25_wait_busy(timeout_ms) == 0) ? 0 : -3;
}

int w25_erase_sector(uint32_t addr)
{
    /* 4 KB sector erase: 45 ms typical, 400 ms max. */
    return erase_generic(CMD_SECTOR_ERASE, addr & ~(W25_SECTOR_SIZE - 1u), 1000);
}

int w25_erase_block(uint32_t addr)
{
    /* 64 KB block erase: 150 ms typical, 1 s max. */
    return erase_generic(CMD_BLOCK_ERASE64, addr & ~(W25_BLOCK_SIZE - 1u), 3000);
}

int w25_fast_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (addr + len > W25_TOTAL_BYTES) return -1;
    if (w25_wait_busy(1000) != 0) return -2;
    cs_low();
    cmd_addr(CMD_FAST_READ, addr);
    (void)spi_xfer(0xFF);            /* Fast Read needs one dummy byte */
    spi_read(buf, len);
    cs_high();
    return 0;
}

/* --- Self test ----------------------------------------------------------- */

int w25_self_test(uint32_t addr, int32_t *mismatch)
{
    uint8_t tx[W25_PAGE_SIZE], rx[W25_PAGE_SIZE];
    if (mismatch) *mismatch = -1;

    /* A counter mixed with the address catches stuck bits, swapped data lines
     * and a chip that simply echoes back whatever was last on the bus. */
    for (uint32_t i = 0; i < W25_PAGE_SIZE; i++)
        tx[i] = (uint8_t)(i ^ 0x5Au);

    int rc = w25_erase_sector(addr);
    if (rc != 0) return -10 + rc;

    /* Erased NOR reads as 0xFF; verify that before trusting the write. */
    rc = w25_fast_read(addr, rx, W25_PAGE_SIZE);
    if (rc != 0) return -20 + rc;
    for (uint32_t i = 0; i < W25_PAGE_SIZE; i++) {
        if (rx[i] != 0xFF) { if (mismatch) *mismatch = (int32_t)i; return -30; }
    }

    rc = w25_page_program(addr, tx, W25_PAGE_SIZE);
    if (rc != 0) return -40 + rc;

    rc = w25_fast_read(addr, rx, W25_PAGE_SIZE);
    if (rc != 0) return -50 + rc;
    for (uint32_t i = 0; i < W25_PAGE_SIZE; i++) {
        if (rx[i] != tx[i]) { if (mismatch) *mismatch = (int32_t)i; return -60; }
    }
    return 0;
}
