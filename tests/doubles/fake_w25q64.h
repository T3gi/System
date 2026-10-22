/* Fake W25Q64 behind the SPI platform interface (implements platform_spi.h).
 *
 * A working model of the chip: 8 MB of memory, status register with BUSY and
 * WEL, and the commands the driver uses (06 04 05 9F AB 02 20 D8 03 0B).
 * Writes and erases only take effect after Write Enable, like on the real chip.
 * Each CS# frame is logged as "SPI <first bytes>;", e.g. "SPI D8 00 00 00;". */
#ifndef FAKE_W25Q64_H
#define FAKE_W25Q64_H

#include <stdbool.h>
#include <stdint.h>

#define FAKE_W25_SIZE  (8u * 1024u * 1024u)

typedef enum {
    FAKE_W25_OK,        /* chip answers                                 */
    FAKE_W25_ABSENT     /* MISO floats high: every byte reads FF        */
} fake_w25_line_t;

/* Erased memory, chip idle, default settings. */
void fake_w25_reset(void);

extern fake_w25_line_t fake_w25_line;
extern uint8_t fake_w25_jedec[3];   /* answer to 9F, default EF 40 17           */
extern int  fake_w25_busy_polls;    /* status reads that show BUSY after an erase
                                       or page program, default 3             */
extern bool fake_w25_busy_forever;  /* the next erase/program never finishes    */
extern bool fake_w25_write_protect; /* WP# low: Write Enable is ignored         */

uint8_t *fake_w25_mem(void);
uint32_t fake_w25_last_br(void);     /* br of the last RegInit/SetSpeed */

#endif
