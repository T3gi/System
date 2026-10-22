/* W25Q64 driver against the fake chip: identification, erase/program/read,
 * timeout and refused writes. */
#include <string.h>
#include "unity.h"
#include "w25q64.h"
#include "fake_w25q64.h"
#include "fake_time.h"
#include "bus_log.h"
#include "uart_capture.h"

void setUp(void)
{
    fake_w25_reset();
    fake_time_reset();
    bus_log_reset();
    uart_reset();
}

void tearDown(void) {}

/* --- identification ---------------------------------------------------- */

/* FINIT, capture 12272.9 ms: Release Power-down (AB), 1 ms, then JEDEC ID. */
static void test_init_releases_power_down_then_reads_id(void)
{
    TEST_ASSERT_EQUAL_INT(0, w25_init(4));
    TEST_ASSERT_EQUAL_UINT32(4, fake_w25_last_br());
    TEST_ASSERT_EQUAL_STRING("SPIINIT 4;SPI AB;DELAY 1;SPI 9F FF FF FF;", bus_log_str());
}

/* Open MISO (no chip, no power) reads FF FF FF. */
static void test_read_id_detects_missing_chip(void)
{
    fake_w25_line = FAKE_W25_ABSENT;
    uint8_t m = 0;
    TEST_ASSERT_EQUAL_INT(-1, w25_read_id(&m, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(-2, w25_init(4));
}

/* Another vendor's chip (Macronix MX25L6433F: C2 20 17) is rejected. */
static void test_init_rejects_unexpected_manufacturer(void)
{
    fake_w25_jedec[0] = 0xC2; fake_w25_jedec[1] = 0x20; fake_w25_jedec[2] = 0x17;
    TEST_ASSERT_EQUAL_INT(-3, w25_init(4));
}

/* Boot-time check (HAL path) prints the same text as the firmware UART log. */
static void test_boot_read_id_prints_result(void)
{
    TEST_ASSERT_TRUE(W25Q64_ReadID());
    TEST_ASSERT_EQUAL_STRING("[W25Q64] JEDEC ID: 0xEF 0x40 0x17 (OK - W25Q64 знайдено)\r\n", uart_out());
    TEST_ASSERT_EQUAL_STRING("SPI 9F 00 00 00;", bus_log_str());
}

static void test_boot_read_id_reports_missing_chip(void)
{
    fake_w25_line = FAKE_W25_ABSENT;
    TEST_ASSERT_FALSE(W25Q64_ReadID());
    TEST_ASSERT_EQUAL_STRING("[W25Q64] JEDEC ID: 0xFF 0xFF 0xFF (Помилка або невідомий чіп)\r\n", uart_out());
}

/* --- erase ------------------------------------------------------------- */

/* FERASE 1, capture 15676.5 ms: 05 (free), 06, 05 (WEL), D8 000000, 05 ... until idle. */
static void test_erase_block_sequence_matches_capture(void)
{
    fake_w25_busy_polls = 3;
    memset(fake_w25_mem(), 0x00, 2 * W25_BLOCK_SIZE);
    TEST_ASSERT_EQUAL_INT(0, w25_erase_block(0));
    TEST_ASSERT_EQUAL_STRING("SPI 05 FF;SPI 06;SPI 05 FF;SPI D8 00 00 00;"
                             "SPI 05 FF;SPI 05 FF;SPI 05 FF;SPI 05 FF;", bus_log_str());
    /* The block reads as erased, the next one is untouched, WEL is cleared. */
    TEST_ASSERT_EACH_EQUAL_HEX8(0xFF, fake_w25_mem(), W25_BLOCK_SIZE);
    TEST_ASSERT_EQUAL_HEX8(0x00, fake_w25_mem()[W25_BLOCK_SIZE]);
    TEST_ASSERT_EQUAL_HEX8(0x00, w25_read_status());
}

/* WP# held low: WEL never sets, so the erase command must not be sent. */
static void test_erase_refused_without_write_enable(void)
{
    fake_w25_write_protect = true;
    TEST_ASSERT_EQUAL_INT(-2, w25_erase_block(0));
    TEST_ASSERT_EQUAL_INT(0, bus_log_count("SPI D8"));
}

/* BUSY never clears after D8: timeout instead of hanging. */
static void test_erase_times_out_if_busy_never_clears(void)
{
    fake_w25_busy_forever = true;
    TEST_ASSERT_EQUAL_INT(-3, w25_erase_block(0));
    TEST_ASSERT_TRUE(fake_time_now() > 3000);   /* waited the full 3 s timeout */
}

/* --- program / read ---------------------------------------------------- */

/* "Data read back from flash matches what was written, byte for byte." */
static void test_program_then_read_back_is_identical(void)
{
    uint8_t tx[W25_PAGE_SIZE], rx[W25_PAGE_SIZE];
    for (unsigned i = 0; i < sizeof tx; i++) tx[i] = (uint8_t)(i * 7 + 3);
    TEST_ASSERT_EQUAL_INT(0, w25_erase_sector(0x2000));
    TEST_ASSERT_EQUAL_INT(0, w25_page_program(0x2000, tx, sizeof tx));
    TEST_ASSERT_EQUAL_INT(0, w25_fast_read(0x2000, rx, sizeof rx));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(tx, rx, sizeof tx);
}

static void test_program_refused_without_write_enable(void)
{
    uint8_t d[4] = { 1, 2, 3, 4 };
    fake_w25_write_protect = true;
    TEST_ASSERT_EQUAL_INT(-4, w25_page_program(0, d, sizeof d));
    TEST_ASSERT_EQUAL_INT(0, bus_log_count("SPI 02"));
    TEST_ASSERT_EQUAL_HEX8(0xFF, fake_w25_mem()[0]);
}

/* --- self test (FTEST command) ----------------------------------------- */

static void test_self_test_passes_on_working_chip(void)
{
    int32_t bad = 123;
    TEST_ASSERT_EQUAL_INT(0, w25_self_test(0x3000, &bad));
    TEST_ASSERT_EQUAL_INT32(-1, bad);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_releases_power_down_then_reads_id);
    RUN_TEST(test_read_id_detects_missing_chip);
    RUN_TEST(test_init_rejects_unexpected_manufacturer);
    RUN_TEST(test_boot_read_id_prints_result);
    RUN_TEST(test_boot_read_id_reports_missing_chip);
    RUN_TEST(test_erase_block_sequence_matches_capture);
    RUN_TEST(test_erase_refused_without_write_enable);
    RUN_TEST(test_erase_times_out_if_busy_never_clears);
    RUN_TEST(test_program_then_read_back_is_identical);
    RUN_TEST(test_program_refused_without_write_enable);
    RUN_TEST(test_self_test_passes_on_working_chip);
    return UNITY_END();
}
