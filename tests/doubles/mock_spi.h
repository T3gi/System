/* Mock SPI bus (implements platform_spi.h).
 *
 * A mock knows nothing about the chip.  The test lists, in order, every CS#
 * frame the driver must send and what MISO answers in it; the mock replays the
 * answers and records any deviation: a wrong byte, a frame of the wrong
 * length, a missing frame or an extra one.  mock_spi_verify() fails the test on
 * the first deviation.  This checks the command *sequence* — e.g. that Write
 * Enable precedes an erase and nothing at all follows a refused one — which a
 * fake that only models state would let slip. */
#ifndef MOCK_SPI_H
#define MOCK_SPI_H

#include <stddef.h>
#include <stdint.h>

void mock_spi_reset(void);
/* Expect one CS# frame: tx bytes on MOSI, rx bytes returned on MISO
 * (rx may be NULL: MISO reads FF). */
void mock_spi_expect(const uint8_t *tx, size_t n, const uint8_t *rx);
/* Fails the current Unity test unless every expected frame came, in order. */
void mock_spi_verify(void);

#define MOCK_SPI_TX(...) \
    mock_spi_expect((const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}), NULL)
#define MOCK_SPI_TXRX(tx, rx) \
    mock_spi_expect(tx, sizeof(tx), rx)

#endif
