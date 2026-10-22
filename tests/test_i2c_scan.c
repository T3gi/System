/* I2C bus scan. */
#include <string.h>
#include "unity.h"
#include "i2c_scan.h"
#include "fake_i2c.h"
#include "uart_capture.h"

void setUp(void)
{
    fake_i2c_reset();
    uart_reset();
}

void tearDown(void) {}

/* Every 7-bit address 0x01..0x7F is probed exactly once, 0x00 (general call) never. */
static void test_probes_every_7bit_address(void)
{
    I2C_Scan();
    TEST_ASSERT_EQUAL_INT(127, fake_i2c_probe_count());
    TEST_ASSERT_FALSE(fake_i2c_was_probed(0x00));
    for (unsigned a = 1; a < 128; a++) TEST_ASSERT_TRUE(fake_i2c_was_probed((uint8_t)(a << 1)));
}

/* Only the MPU6050 answers: one line with its 7-bit address and a count of 1. */
static void test_reports_found_device(void)
{
    fake_i2c_add(0x68 << 1);
    I2C_Scan();
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), " Знайдено пристрій за адресою: 0x68\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "Знайдено пристроїв: 1\r\n"));
}

static void test_empty_bus_reports_zero(void)
{
    I2C_Scan();
    TEST_ASSERT_NULL(strstr(uart_out(), "за адресою"));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "Знайдено пристроїв: 0\r\n"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_probes_every_7bit_address);
    RUN_TEST(test_reports_found_device);
    RUN_TEST(test_empty_bus_reports_zero);
    return UNITY_END();
}
