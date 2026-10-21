/**
 ******************************************************************************
 * @file    w25q64.h
 * @brief   Winbond W25Q64 (64 Mbit / 8 MB) SPI NOR flash driver.
 *
 * Wiring (SPI2 — the only pin group that collides with nothing already in use;
 * the camera occupies PB4/PB5/PB10 and PC0..PC7, SCCB PB8/PB9, XCLK PA8):
 *
 *   W25Q64 pin        signal            STM32F401RE
 *   1  CS#            chip select       PB12   (plain GPIO, idle HIGH)
 *   2  DO   (IO1)     MISO              PB14   (SPI2_MISO, AF5)
 *   3  WP#  (IO2)     write protect     3.3 V  <- MUST be tied high
 *   4  GND            ground            GND
 *   5  DI   (IO0)     MOSI              PB15   (SPI2_MOSI, AF5)
 *   6  CLK            clock             PB13   (SPI2_SCK,  AF5)
 *   7  HOLD#(IO3)     hold/reset        3.3 V  <- MUST be tied high
 *   8  VCC            3.3 V             3V3    + 100 nF close to the package
 *
 * The chip is 2.7-3.6 V and is NOT 5 V tolerant.
 *
 * Leaving WP# or HOLD# floating is the classic cause of "the flash works
 * intermittently": a floating WP# lets writes fail silently and a floating
 * HOLD# stalls transactions at random.
 ******************************************************************************
 */
#ifndef INC_W25Q64_H_
#define INC_W25Q64_H_

#include <stdint.h>

/* Geometry */
#define W25_PAGE_SIZE     256u
#define W25_SECTOR_SIZE   4096u
#define W25_BLOCK_SIZE    65536u
#define W25_TOTAL_BYTES   (8u * 1024u * 1024u)

/* Expected JEDEC ID: Winbond 0xEF, type 0x40, capacity 0x17 (64 Mbit). */
#define W25_JEDEC_MFG     0xEFu
#define W25_JEDEC_TYPE    0x40u
#define W25_JEDEC_CAP64   0x17u

/* Status register 1 bits */
#define W25_SR1_BUSY      0x01u
#define W25_SR1_WEL       0x02u

/**
 * @brief Boot-time JEDEC ID check through HAL SPI2 (CubeMX settings) and
 *        print the result.  Does not touch the register-path setup below.
 */
void W25Q64_ReadID(void);

/**
 * @brief Bring up SPI2 + CS and read the JEDEC ID.
 * @param br SPI baud-rate divider index: clock = APB1 / 2^(br+1).
 *        SPI2 lives on APB1 = 42 MHz, so br=0 -> 21 MHz and br=4 -> 1.3 MHz.
 *        Start high (slow) on jumper wires; short PCB traces tolerate br=0.
 * @return 0 if the JEDEC ID matches a W25Q64, negative otherwise.
 */
int  w25_init(uint32_t br);

/** Change the SPI clock without re-reading the ID.  Same br scale as above. */
void w25_set_speed(uint32_t br);

/** Read the 3-byte JEDEC ID.  Returns 0 on success. */
int  w25_read_id(uint8_t *mfg, uint8_t *type, uint8_t *cap);

/** Read status register 1 (bit0 = BUSY, bit1 = WEL). */
uint8_t w25_read_status(void);

/** Poll BUSY until clear.  Returns 0 on success, -1 on timeout. */
int  w25_wait_busy(uint32_t timeout_ms);

/** Program up to 256 bytes, must not cross a page boundary.  0 on success. */
int  w25_page_program(uint32_t addr, const uint8_t *data, uint32_t len);

/** Erase one 4 KB sector / one 64 KB block.  0 on success. */
int  w25_erase_sector(uint32_t addr);
int  w25_erase_block(uint32_t addr);

/** Fast Read (0x0B).  Any length, crosses pages/sectors freely. */
int  w25_fast_read(uint32_t addr, uint8_t *buf, uint32_t len);

/**
 * @brief Erase one sector, write a pattern, read it back and compare.
 * @param addr     sector-aligned address to use
 * @param mismatch out: index of the first differing byte, or -1
 * @return 0 if the read-back matched exactly.
 */
int  w25_self_test(uint32_t addr, int32_t *mismatch);

#endif /* INC_W25Q64_H_ */
