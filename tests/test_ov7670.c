/* OV7670 configuration over SCCB, checked against the bus traffic decoded
 * from i2c_init_temp_check.sr. */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "ov7670.h"
#include "camera_blob.h"
#include "fake_sccb.h"
#include "fake_time.h"
#include "bus_log.h"
#include "uart_capture.h"
#include "ov7670_cif_init_vector.h"

#define N_VECTOR (sizeof ov7670_cif_init_vector / sizeof ov7670_cif_init_vector[0])

void setUp(void)
{
    fake_sccb_reset();
    fake_time_reset();
    bus_log_reset();
    uart_reset();
}

void tearDown(void) {}

/* Must run first: before any MODE command the firmware is in CIF. */
static void test_default_mode_is_cif(void)
{
    TEST_ASSERT_EQUAL_INT(1, ov7670_current_mode());
    TEST_ASSERT_EQUAL_STRING("CIF", ov7670_mode_info(1)->name);
}

/* The whole CIF bring-up, 5330.1-5488.8 ms of the capture: 137 SCCB
 * transactions, compared one by one (including the two read-modify-writes). */
static void test_configure_cif_matches_capture(void)
{
    /* Power-on values the sensor returned in the capture for the RMW reads. */
    fake_sccb_set_reg(REG_HREF, 0xB6);
    fake_sccb_set_reg(REG_VREF, 0x0A);

    ov7670_configure(1);

    TEST_ASSERT_EQUAL_UINT(N_VECTOR, fake_sccb_count());
    const sccb_vec_t *got = fake_sccb_trace();
    for (unsigned i = 0; i < N_VECTOR; i++) {
        const sccb_vec_t *exp = &ov7670_cif_init_vector[i];
        char msg[80];
        snprintf(msg, sizeof msg, "transaction %u: op %d reg %02X val %02X", i, exp->op, exp->reg, exp->val);
        TEST_ASSERT_EQUAL_INT_MESSAGE(exp->op, got[i].op, msg);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(exp->reg, got[i].reg, msg);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(exp->val, got[i].val, msg);
    }
    TEST_ASSERT_EQUAL_HEX8(OV7670_SCCB_ADDR, fake_sccb_last_slave());
}

/* Capture: 99.3 ms of silence after COM7=80, 10.1 / 10.6 ms inside the RMWs. */
static void test_configure_waits_after_reset_and_in_rmw(void)
{
    ov7670_configure(1);
    const char *log = bus_log_str();
    TEST_ASSERT_EQUAL_STRING_LEN("SCCB W 21 12 80;DELAY 100;SCCB W 21 6B 7A;", log, 42);
    TEST_ASSERT_NOT_NULL(strstr(log, "SCCB R 21 32;DELAY 10;SCCB W 21 32"));
    TEST_ASSERT_NOT_NULL(strstr(log, "SCCB R 21 03;DELAY 10;SCCB W 21 03"));
    TEST_ASSERT_EQUAL_INT(3, bus_log_count("DELAY"));
}

/* Clock chain from analysis.md §4: DBLV = 7A (PLL x4), CLKRC = BF (/64). */
static void test_configure_cif_sets_clock_and_window(void)
{
    fake_sccb_set_reg(REG_HREF, 0xB6);
    ov7670_configure(1);
    TEST_ASSERT_EQUAL_HEX8(0x7A, fake_sccb_reg(REG_DBLV));
    TEST_ASSERT_EQUAL_HEX8(0xBF, fake_sccb_reg(REG_CLKRC));
    TEST_ASSERT_EQUAL_INT(64, (fake_sccb_reg(REG_CLKRC) & 0x3F) + 1);
    TEST_ASSERT_EQUAL_HEX8(170 >> 3, fake_sccb_reg(REG_HSTART));
    TEST_ASSERT_EQUAL_HEX8(90 >> 3, fake_sccb_reg(REG_HSTOP));
    TEST_ASSERT_EQUAL_HEX8(0x92, fake_sccb_reg(REG_HREF));   /* B6 & C0 | 2<<3 | 2 */
    TEST_ASSERT_EQUAL_STRING("MODE 1 CIF nominal=352x288 window=h[170..90] v[14..494]\r\n", uart_out());
    TEST_ASSERT_EQUAL_INT(1, ov7670_current_mode());
}

/* A failed read in the RMW falls back to documented defaults instead of
 * writing garbage: HREF top bits 10, VREF top bits 0000. */
static void test_set_hw_uses_defaults_when_read_fails(void)
{
    fake_sccb_fail_reads(true);
    ov7670_set_hw(170, 90, 14, 494);
    TEST_ASSERT_EQUAL_HEX8(0x80 | (2 << 3) | 2, fake_sccb_reg(REG_HREF));
    TEST_ASSERT_EQUAL_HEX8((2 << 2) | 2, fake_sccb_reg(REG_VREF));
}

/* FREEZE clears AGC|AWB|AEC in COM8 and keeps the other bits; AUTO sets them. */
static void test_freeze_and_release_auto_loops(void)
{
    fake_sccb_set_reg(REG_COM8, 0xE7);
    ov7670_freeze_auto(1);
    TEST_ASSERT_EQUAL_HEX8(0xE0, fake_sccb_reg(REG_COM8));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "AUTO frozen COM8=0xE0"));
    ov7670_freeze_auto(0);
    TEST_ASSERT_EQUAL_HEX8(0xE7, fake_sccb_reg(REG_COM8));
}

static void test_freeze_uses_default_com8_when_read_fails(void)
{
    fake_sccb_fail_reads(true);
    ov7670_freeze_auto(1);
    TEST_ASSERT_EQUAL_HEX8(0xE0, fake_sccb_reg(REG_COM8));
}

/* Table sanity: every mode exists, has a sensible size and a terminated
 * register list, and the lookup refuses out-of-range indices. */
static void test_mode_table_is_consistent(void)
{
    TEST_ASSERT_EQUAL_UINT(8, ov7670_mode_count());
    TEST_ASSERT_NULL(ov7670_mode_info(-1));
    TEST_ASSERT_NULL(ov7670_mode_info((int)ov7670_mode_count()));
    for (int i = 0; i < (int)ov7670_mode_count(); i++) {
        const ov7670_mode_t *m = ov7670_mode_info(i);
        TEST_ASSERT_NOT_NULL(m);
        TEST_ASSERT_TRUE(m->width > 0 && m->width <= 656 && m->height > 0 && m->height <= 488);
        TEST_ASSERT_TRUE(m->nregs > 0);
        TEST_ASSERT_EQUAL_HEX8(0xFF, m->regs[m->nregs - 1][0]);
        TEST_ASSERT_EQUAL_HEX8(0xFF, m->regs[m->nregs - 1][1]);
    }
}

static void test_configure_out_of_range_falls_back_to_mode_0(void)
{
    ov7670_configure(99);
    TEST_ASSERT_EQUAL_INT(0, ov7670_current_mode());
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "MODE 0 VGA"));
    ov7670_configure(1);
    ov7670_configure(-1);
    TEST_ASSERT_EQUAL_INT(0, ov7670_current_mode());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_mode_is_cif);
    RUN_TEST(test_configure_cif_matches_capture);
    RUN_TEST(test_configure_waits_after_reset_and_in_rmw);
    RUN_TEST(test_configure_cif_sets_clock_and_window);
    RUN_TEST(test_set_hw_uses_defaults_when_read_fails);
    RUN_TEST(test_freeze_and_release_auto_loops);
    RUN_TEST(test_freeze_uses_default_com8_when_read_fails);
    RUN_TEST(test_mode_table_is_consistent);
    RUN_TEST(test_configure_out_of_range_falls_back_to_mode_0);
    return UNITY_END();
}
