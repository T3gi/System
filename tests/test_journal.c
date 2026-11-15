/* App: event journal (FR-07) against the fake W25Q64: record format, restart,
 * corrupted records, the ring, and a flash that refuses to work. */
#include <string.h>
#include "unity.h"
#include "journal.h"
#include "damper.h"
#include "w25q64.h"
#include "fake_w25q64.h"
#include "fake_time.h"
#include "bus_log.h"
#include "uart_capture.h"

#define BASE          (W25_TOTAL_BYTES - JOURNAL_SECTORS * W25_SECTOR_SIZE)
#define SLOTS         (JOURNAL_SECTORS * W25_SECTOR_SIZE / JOURNAL_RECORD_SIZE)
#define PER_SECTOR    (W25_SECTOR_SIZE / JOURNAL_RECORD_SIZE)

void setUp(void)
{
    fake_w25_reset();
    fake_time_reset();
    bus_log_reset();
    uart_reset();
}

void tearDown(void) {}

static uint8_t *slot(uint32_t n)
{
    return fake_w25_mem() + BASE + n * JOURNAL_RECORD_SIZE;
}

/* Sequence number stored in a slot. */
static uint32_t slot_seq(uint32_t n)
{
    uint32_t seq;
    memcpy(&seq, slot(n), sizeof seq);
    return seq;
}

static void append_n(unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        TEST_ASSERT_TRUE(Journal_Append(DAMPER_OPEN, DAMPER_SRC_TEMP, 2650, 1000u + i));
    }
}

/* Journal_Init + clean UART, so a test can compare only what it prints next. */
static void init_quiet(void)
{
    TEST_ASSERT_TRUE(Journal_Init());
    uart_reset();
}

static int count_printed(const char *needle)
{
    int n = 0;
    size_t step = strlen(needle);
    for (const char *p = uart_out(); (p = strstr(p, needle)) != NULL; p += step) n++;
    return n;
}

/* --- start --------------------------------------------------------------- */

/* 1.3 MHz, the FINIT default that is reliable on jumper wires. */
static void test_init_on_an_empty_chip(void)
{
    TEST_ASSERT_TRUE(Journal_Init());
    TEST_ASSERT_EQUAL_UINT32(4, fake_w25_last_br());
    TEST_ASSERT_EQUAL_STRING("JOURNAL records=0 bad=0 next_seq=1\r\n", uart_out());
}

static void test_empty_journal_prints_empty(void)
{
    init_quiet();
    Journal_PrintLast(10);
    TEST_ASSERT_EQUAL_STRING("LOG empty\r\n", uart_out());
}

/* No chip: reported, and the rest of the firmware gets false everywhere. */
static void test_missing_chip_is_reported(void)
{
    fake_w25_line = FAKE_W25_ABSENT;
    TEST_ASSERT_FALSE(Journal_Init());
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "JOURNAL ERR flash init rc=-2\r\n"));
    TEST_ASSERT_FALSE(Journal_Append(DAMPER_OPEN, DAMPER_SRC_TEMP, 2650, 1000));
    uart_reset();
    Journal_PrintLast(10);
    TEST_ASSERT_EQUAL_STRING("LOG empty\r\n", uart_out());
}

/* --- record -------------------------------------------------------------- */

/* The 16 bytes on the flash, computed independently of the firmware:
 * python3: zlib.crc32(struct.pack('<IIhBB', 1, 1000, 2650, 1, 1)) = 0x4696C58E */
static void test_record_format_on_the_flash(void)
{
    static const uint8_t expected[JOURNAL_RECORD_SIZE] = {
        0x01, 0x00, 0x00, 0x00,     /* seq 1            */
        0xE8, 0x03, 0x00, 0x00,     /* t_ms 1000        */
        0x5A, 0x0A,                 /* temp_c100 2650   */
        0x01,                       /* DAMPER_OPEN      */
        0x01,                       /* DAMPER_SRC_TEMP  */
        0x8E, 0xC5, 0x96, 0x46,     /* crc32            */
    };
    init_quiet();
    TEST_ASSERT_TRUE(Journal_Append(DAMPER_OPEN, DAMPER_SRC_TEMP, 2650, 1000));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, slot(0), JOURNAL_RECORD_SIZE);
}

/* FR-07: state and source are printed in words. */
static void test_printed_line_for_each_state_and_source(void)
{
    init_quiet();
    Journal_Append(DAMPER_OPEN,   DAMPER_SRC_TEMP,   2650, 1000);
    Journal_Append(DAMPER_CLOSED, DAMPER_SRC_CAMERA, 2400, 2000);
    Journal_Append(DAMPER_CLOSED, DAMPER_SRC_NONE,   2305, 3000);
    Journal_PrintLast(10);
    TEST_ASSERT_EQUAL_STRING(
        "LOG seq=1 t=1000 state=OPEN src=TEMP temp=26.50\r\n"
        "LOG seq=2 t=2000 state=CLOSED src=CAMERA temp=24.00\r\n"
        "LOG seq=3 t=3000 state=CLOSED src=NONE temp=23.05\r\n",
        uart_out());
}

static void test_negative_temperatures_keep_their_sign(void)
{
    init_quiet();
    Journal_Append(DAMPER_CLOSED, DAMPER_SRC_TEMP, -150, 1000);
    Journal_Append(DAMPER_CLOSED, DAMPER_SRC_TEMP, -50,  2000);
    Journal_PrintLast(10);
    TEST_ASSERT_EQUAL_STRING(
        "LOG seq=1 t=1000 state=CLOSED src=TEMP temp=-1.50\r\n"
        "LOG seq=2 t=2000 state=CLOSED src=TEMP temp=-0.50\r\n",
        uart_out());
}

/* --- printing ------------------------------------------------------------ */

/* PRD test 3: the last n events, oldest first. */
static void test_prints_the_last_n_oldest_first(void)
{
    init_quiet();
    append_n(12);
    Journal_PrintLast(2);
    TEST_ASSERT_EQUAL_STRING(
        "LOG seq=11 t=1010 state=OPEN src=TEMP temp=26.50\r\n"
        "LOG seq=12 t=1011 state=OPEN src=TEMP temp=26.50\r\n",
        uart_out());
}

static void test_prints_all_when_there_are_fewer_than_n(void)
{
    init_quiet();
    append_n(3);
    Journal_PrintLast(10);
    TEST_ASSERT_EQUAL_INT(3, count_printed("LOG seq="));
}

/* The print buffer holds 16 records; asking for more must not overrun it. */
static void test_print_count_is_limited(void)
{
    init_quiet();
    append_n(20);
    Journal_PrintLast(100);
    TEST_ASSERT_EQUAL_INT(16, count_printed("LOG seq="));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "LOG seq=5 "));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "LOG seq=20 "));
}

/* --- restart -------------------------------------------------------------- */

/* PRD test 3: records survive a reset and the numbering continues. */
static void test_restart_finds_the_records_and_continues(void)
{
    init_quiet();
    append_n(3);

    TEST_ASSERT_TRUE(Journal_Init());
    TEST_ASSERT_EQUAL_STRING("JOURNAL records=3 bad=0 next_seq=4\r\n", uart_out());

    TEST_ASSERT_TRUE(Journal_Append(DAMPER_OPEN, DAMPER_SRC_TEMP, 2650, 1000));
    TEST_ASSERT_EQUAL_UINT32(4, slot_seq(3));
}

/* A record with a bad CRC is counted and never printed. */
static void test_corrupted_record_is_skipped(void)
{
    init_quiet();
    append_n(3);
    slot(1)[8] ^= 0x01;

    TEST_ASSERT_TRUE(Journal_Init());
    TEST_ASSERT_EQUAL_STRING("JOURNAL records=2 bad=1 next_seq=4\r\n", uart_out());

    uart_reset();
    Journal_PrintLast(10);
    TEST_ASSERT_EQUAL_INT(2, count_printed("LOG seq="));
    TEST_ASSERT_NULL(strstr(uart_out(), "seq=2 "));
}

/* Power lost during a write: that slot is stepped over, not written twice. */
static void test_half_written_slot_is_stepped_over(void)
{
    init_quiet();
    append_n(1);
    slot(1)[0] = 0x12;
    init_quiet();

    TEST_ASSERT_TRUE(Journal_Append(DAMPER_OPEN, DAMPER_SRC_TEMP, 2650, 2000));
    TEST_ASSERT_EQUAL_UINT8(0x12, slot(1)[0]);
    TEST_ASSERT_EQUAL_UINT32(2, slot_seq(2));
}

/* --- ring ----------------------------------------------------------------- */

/* Entering a sector erases it: old data there does not block the new record. */
static void test_entering_a_sector_erases_it(void)
{
    init_quiet();
    append_n(PER_SECTOR);
    memset(slot(PER_SECTOR), 0x00, W25_SECTOR_SIZE);

    TEST_ASSERT_TRUE(Journal_Append(DAMPER_OPEN, DAMPER_SRC_TEMP, 2650, 1000));
    TEST_ASSERT_EQUAL_UINT32(PER_SECTOR + 1u, slot_seq(PER_SECTOR));
    TEST_ASSERT_EQUAL_UINT8(0xFF, slot(PER_SECTOR + 1u)[0]);
}

/* A full lap: the newest go over the oldest sector, the rest stay. */
static void test_ring_wraps_over_the_oldest_sector(void)
{
    init_quiet();
    append_n(SLOTS + 5u);

    TEST_ASSERT_EQUAL_UINT32(SLOTS + 1u, slot_seq(0));
    TEST_ASSERT_EQUAL_UINT32(SLOTS + 5u, slot_seq(4));
    TEST_ASSERT_EQUAL_UINT8(0xFF, slot(5)[0]);
    TEST_ASSERT_EQUAL_UINT32(PER_SECTOR + 1u, slot_seq(PER_SECTOR));
}

/* After a lap a restart still finds the newest record, not the highest slot. */
static void test_restart_after_a_lap_finds_the_newest(void)
{
    init_quiet();
    append_n(SLOTS + 5u);

    TEST_ASSERT_TRUE(Journal_Init());
    TEST_ASSERT_EQUAL_STRING("JOURNAL records=773 bad=0 next_seq=1030\r\n", uart_out());

    TEST_ASSERT_TRUE(Journal_Append(DAMPER_OPEN, DAMPER_SRC_TEMP, 2650, 1000));
    TEST_ASSERT_EQUAL_UINT32(1030, slot_seq(5));
}

/* The last events are printed in order across the end of the ring. */
static void test_print_across_the_end_of_the_ring(void)
{
    init_quiet();
    append_n(SLOTS + 2u);
    Journal_PrintLast(4);
    const char *a = strstr(uart_out(), "seq=1023 ");
    const char *b = strstr(uart_out(), "seq=1024 ");
    const char *c = strstr(uart_out(), "seq=1025 ");
    const char *d = strstr(uart_out(), "seq=1026 ");
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_TRUE(a < b && b < c && c < d);
}

/* Saved frames (FERASE/FTEST) live from address 0: the ring stays at the top. */
static void test_journal_stays_inside_its_sectors(void)
{
    fake_w25_mem()[0] = 0xAA;
    fake_w25_mem()[BASE - 1u] = 0x55;
    init_quiet();
    append_n(SLOTS + 5u);
    TEST_ASSERT_EQUAL_UINT8(0xAA, fake_w25_mem()[0]);
    TEST_ASSERT_EQUAL_UINT8(0x55, fake_w25_mem()[BASE - 1u]);
}

/* --- flash refuses to work ------------------------------------------------- */

/* Slot 0 starts a sector, so the first step is the erase. */
static void test_refused_erase_is_reported(void)
{
    init_quiet();
    fake_w25_write_protect = true;
    TEST_ASSERT_FALSE(Journal_Append(DAMPER_OPEN, DAMPER_SRC_TEMP, 2650, 1000));
}

/* Slot 1 needs no erase, so here the page program fails.  The record does not
 * count: the next good write goes to the same slot with the same number. */
static void test_refused_write_is_reported_and_retried(void)
{
    init_quiet();
    append_n(1);
    fake_w25_write_protect = true;
    TEST_ASSERT_FALSE(Journal_Append(DAMPER_OPEN, DAMPER_SRC_TEMP, 2650, 2000));

    fake_w25_write_protect = false;
    TEST_ASSERT_TRUE(Journal_Append(DAMPER_OPEN, DAMPER_SRC_TEMP, 2650, 3000));
    TEST_ASSERT_EQUAL_UINT32(2, slot_seq(1));
}

/* A chip that stays busy returns false instead of hanging the main loop. */
static void test_stuck_chip_does_not_hang(void)
{
    init_quiet();
    fake_w25_busy_forever = true;
    TEST_ASSERT_FALSE(Journal_Append(DAMPER_OPEN, DAMPER_SRC_TEMP, 2650, 1000));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_on_an_empty_chip);
    RUN_TEST(test_empty_journal_prints_empty);
    RUN_TEST(test_missing_chip_is_reported);
    RUN_TEST(test_record_format_on_the_flash);
    RUN_TEST(test_printed_line_for_each_state_and_source);
    RUN_TEST(test_negative_temperatures_keep_their_sign);
    RUN_TEST(test_prints_the_last_n_oldest_first);
    RUN_TEST(test_prints_all_when_there_are_fewer_than_n);
    RUN_TEST(test_print_count_is_limited);
    RUN_TEST(test_restart_finds_the_records_and_continues);
    RUN_TEST(test_corrupted_record_is_skipped);
    RUN_TEST(test_half_written_slot_is_stepped_over);
    RUN_TEST(test_entering_a_sector_erases_it);
    RUN_TEST(test_ring_wraps_over_the_oldest_sector);
    RUN_TEST(test_restart_after_a_lap_finds_the_newest);
    RUN_TEST(test_print_across_the_end_of_the_ring);
    RUN_TEST(test_journal_stays_inside_its_sectors);
    RUN_TEST(test_refused_erase_is_reported);
    RUN_TEST(test_refused_write_is_reported_and_retried);
    RUN_TEST(test_stuck_chip_does_not_hang);
    return UNITY_END();
}
