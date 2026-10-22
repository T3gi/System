/* W25Q64 command sequences against a strict SPI mock: every CS# frame must
 * come in this exact order, with nothing missing and nothing extra.
 * MISO answers are the bytes seen in spi_init_ferase_test.sr. */
#include "unity.h"
#include "w25q64.h"
#include "mock_spi.h"
#include "fake_time.h"
#include "bus_log.h"

void setUp(void)
{
    mock_spi_reset();
    fake_time_reset();
    bus_log_reset();
}

void tearDown(void) {}

static const uint8_t RDSR[2]        = { 0x05, 0xFF };
static const uint8_t SR_IDLE[2]     = { 0xFF, 0x00 };   /* capture: 05 -> 00 */
static const uint8_t SR_WEL[2]      = { 0xFF, 0x02 };   /* capture: 05 -> 02 */
static const uint8_t SR_BUSY_WEL[2] = { 0xFF, 0x03 };   /* capture: 05 -> 03 */

/* FERASE 1, capture 15676.5-15873.8 ms (about 15 200 polls there, 2 here). */
static void test_erase_block_exact_sequence(void)
{
    MOCK_SPI_TXRX(RDSR, SR_IDLE);
    MOCK_SPI_TX(0x06);
    MOCK_SPI_TXRX(RDSR, SR_WEL);
    MOCK_SPI_TX(0xD8, 0x00, 0x00, 0x00);
    MOCK_SPI_TXRX(RDSR, SR_BUSY_WEL);
    MOCK_SPI_TXRX(RDSR, SR_BUSY_WEL);
    MOCK_SPI_TXRX(RDSR, SR_IDLE);

    TEST_ASSERT_EQUAL_INT(0, w25_erase_block(0));
    mock_spi_verify();
}

/* Write Enable must come right before Page Program, and the data follows the
 * 24-bit address in the same frame. */
static void test_page_program_exact_sequence(void)
{
    const uint8_t data[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    MOCK_SPI_TXRX(RDSR, SR_IDLE);
    MOCK_SPI_TX(0x06);
    MOCK_SPI_TXRX(RDSR, SR_WEL);
    MOCK_SPI_TX(0x02, 0x00, 0x01, 0x00, 0xDE, 0xAD, 0xBE, 0xEF);
    MOCK_SPI_TXRX(RDSR, SR_BUSY_WEL);
    MOCK_SPI_TXRX(RDSR, SR_IDLE);

    TEST_ASSERT_EQUAL_INT(0, w25_page_program(0x000100, data, sizeof data));
    mock_spi_verify();
}

/* WEL did not set: after the status check nothing else may be sent.  The mock
 * fails on any extra frame, which a fake would only notice indirectly. */
static void test_erase_sends_nothing_after_refused_write_enable(void)
{
    MOCK_SPI_TXRX(RDSR, SR_IDLE);
    MOCK_SPI_TX(0x06);
    MOCK_SPI_TXRX(RDSR, SR_IDLE);

    TEST_ASSERT_EQUAL_INT(-2, w25_erase_block(0));
    mock_spi_verify();
}

/* FINIT, capture 12272.9 / 12274.8 ms. */
static void test_init_exact_sequence(void)
{
    static const uint8_t jedec_tx[4] = { 0x9F, 0xFF, 0xFF, 0xFF };
    static const uint8_t jedec_rx[4] = { 0xFF, 0xEF, 0x40, 0x17 };
    MOCK_SPI_TX(0xAB);
    MOCK_SPI_TXRX(jedec_tx, jedec_rx);

    TEST_ASSERT_EQUAL_INT(0, w25_init(4));
    mock_spi_verify();
    TEST_ASSERT_EQUAL_STRING("DELAY 1;", bus_log_str());   /* tRES1 >= 3 us */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_erase_block_exact_sequence);
    RUN_TEST(test_page_program_exact_sequence);
    RUN_TEST(test_erase_sends_nothing_after_refused_write_enable);
    RUN_TEST(test_init_exact_sequence);
    return UNITY_END();
}
