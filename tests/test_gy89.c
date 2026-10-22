/* GY-89 ID check (LSM303D, L3GD20, BMP180) and its UART report. */
#include <string.h>
#include "unity.h"
#include "gy89.h"
#include "fake_i2c.h"
#include "bus_log.h"
#include "uart_capture.h"
#include "capture_vectors.h"

void setUp(void)
{
    fake_i2c_reset();
    bus_log_reset();
    uart_reset();
    fake_i2c_add(LSM303D_ADDR);
    fake_i2c_add(L3GD20_ADDR);
    fake_i2c_add(BMP180_ADDR);
    fake_i2c_set_reg(LSM303D_ADDR, 0x0F, CAP_LSM303D_WHO_AM_I);
    fake_i2c_set_reg(L3GD20_ADDR, 0x0F, CAP_L3GD20_WHO_AM_I);
    fake_i2c_set_reg(BMP180_ADDR, 0xD0, CAP_BMP180_CHIP_ID);
}

void tearDown(void) {}

/* Capture 4989.3-4995.0 ms: one 1-byte register read per chip. */
static void test_reads_one_id_register_per_chip(void)
{
    GY89_ReadIDs();
    TEST_ASSERT_EQUAL_STRING("I2C R 3A 0F 1;I2C R D6 0F 1;I2C R EE D0 1;", bus_log_str());
}

/* The IDs seen on the bus are accepted and printed with "(OK)". */
static void test_capture_ids_are_accepted(void)
{
    TEST_ASSERT_TRUE(GY89_ReadIDs());
    TEST_ASSERT_EQUAL_STRING("[LSM303D] WHO_AM_I: 0x49 (OK)\r\n"
                             "[L3GD20]  WHO_AM_I: 0xD4 (OK)\r\n"
                             "[BMP180]  CHIP_ID:  0x55 (OK)\r\n", uart_out());
}

/* DEFECT-2 (fixed): L3GD20 NACKs.  The driver used to print the LSM303D's ID
 * left in its variable, so the log claimed the gyro answered 0x49. */
static void test_nack_is_not_reported_as_a_valid_id(void)
{
    fake_i2c_reset();
    fake_i2c_add(LSM303D_ADDR);
    fake_i2c_set_reg(LSM303D_ADDR, 0x0F, CAP_LSM303D_WHO_AM_I);
    TEST_ASSERT_FALSE(GY89_ReadIDs());
    TEST_ASSERT_NULL(strstr(uart_out(), "[L3GD20]  WHO_AM_I: 0x49"));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "[L3GD20]  WHO_AM_I: Помилка зчитування по I2C!"));
}

/* DEFECT-3 (fixed): a BMP280 (ID 0x58) instead of BMP180 used to be printed
 * without any warning, and the function returned nothing to check. */
static void test_unexpected_id_is_flagged(void)
{
    fake_i2c_set_reg(BMP180_ADDR, 0xD0, 0x58);
    TEST_ASSERT_FALSE(GY89_ReadIDs());
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "[BMP180]  CHIP_ID:  0x58 (Невірний ID, очікується 0x55)"));
}

/* One bad chip does not stop the check of the others. */
static void test_all_chips_are_checked_after_a_failure(void)
{
    fake_i2c_reset();
    fake_i2c_add(L3GD20_ADDR);
    fake_i2c_add(BMP180_ADDR);
    fake_i2c_set_reg(L3GD20_ADDR, 0x0F, CAP_L3GD20_WHO_AM_I);
    fake_i2c_set_reg(BMP180_ADDR, 0xD0, CAP_BMP180_CHIP_ID);
    TEST_ASSERT_FALSE(GY89_ReadIDs());
    TEST_ASSERT_EQUAL_STRING("I2C R 3A 0F 1;I2C R D6 0F 1;I2C R EE D0 1;", bus_log_str());
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "[L3GD20]  WHO_AM_I: 0xD4 (OK)"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reads_one_id_register_per_chip);
    RUN_TEST(test_capture_ids_are_accepted);
    RUN_TEST(test_nack_is_not_reported_as_a_valid_id);
    RUN_TEST(test_unexpected_id_is_flagged);
    RUN_TEST(test_all_chips_are_checked_after_a_failure);
    return UNITY_END();
}
