/* MPU6050 driver: raw -> °C conversion, multi-byte read, bus errors, init. */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "mpu6050.h"
#include "fake_i2c.h"
#include "bus_log.h"
#include "uart_capture.h"
#include "capture_vectors.h"

#define MPU MPU6050_I2C_ADDR            /* 0xD0 */

void setUp(void)
{
    fake_i2c_reset();
    bus_log_reset();
    uart_reset();
    fake_i2c_add(MPU);
}

void tearDown(void) {}

static float temp_for_raw(uint8_t hi, uint8_t lo)
{
    const uint8_t raw[2] = { hi, lo };
    fake_i2c_set_regs(MPU, MPU6050_REG_TEMP_OUT_H, raw, 2);
    float t = -1000.0f;
    TEST_ASSERT_TRUE(MPU6050_ReadTemperature(&t));
    return t;
}

/* --- raw -> physical units ---------------------------------------------- */

/* MPU-6000/6050 Product Specification Rev 3.1, p.14: sensitivity 340 LSB/°C,
 * offset -521 LSB at 35 °C.  The register-map formula must reproduce it. */
static void test_temperature_reproduces_datasheet_offset_point(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 35.00f, temp_for_raw(0xFD, 0xF7));   /* -521 */
}

/* Register map p.30: T = TEMP_OUT/340 + 36.53, so raw 0 is 36.53 °C. */
static void test_temperature_zero_raw_is_36_53(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 36.53f, temp_for_raw(0x00, 0x00));
}

/* Bytes seen on the bus (i2c_init_temp_check.sr) give the values in analysis.md. */
static void test_temperature_from_capture_vectors(void)
{
    for (unsigned i = 0; i < sizeof capture_temp_vectors / sizeof capture_temp_vectors[0]; i++) {
        const temp_vec_t *v = &capture_temp_vectors[i];
        char msg[64];
        snprintf(msg, sizeof msg, "%02X %02X at %.1f ms", v->hi, v->lo, v->t_ms);
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.005f, v->celsius, temp_for_raw(v->hi, v->lo), msg);
    }
}

/* TEMP_OUT is signed: 0x8000 must not become +32768. */
static void test_temperature_is_signed_16_bit(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 132.90f, temp_for_raw(0x7F, 0xFF));   /* +32767 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -59.85f, temp_for_raw(0x80, 0x00));   /* -32768 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 36.53f - 1.0f / 340.0f, temp_for_raw(0xFF, 0xFF)); /* -1 */
}

/* Both bytes come from one 2-byte read, as on the bus: S 68 W 41 Sr 68 R hi lo P. */
static void test_temperature_is_one_two_byte_read(void)
{
    (void)temp_for_raw(0xF1, 0x80);
    TEST_ASSERT_EQUAL_STRING("I2C R D0 41 2;", bus_log_str());
}

/* --- bus errors -------------------------------------------------------- */

static void test_temperature_nack_returns_false_and_keeps_old_value(void)
{
    float t = 21.5f;
    fake_i2c_fail_next(1);
    TEST_ASSERT_FALSE(MPU6050_ReadTemperature(&t));
    TEST_ASSERT_EQUAL_FLOAT(21.5f, t);
}

static void test_temperature_missing_chip_returns_false(void)
{
    fake_i2c_reset();
    float t = 21.5f;
    TEST_ASSERT_FALSE(MPU6050_ReadTemperature(&t));
    TEST_ASSERT_EQUAL_FLOAT(21.5f, t);
}

/* --- init --------------------------------------------------------------- */

/* Capture 4978.3 / 4981.6 ms: WHO_AM_I = 68, then PWR_MGMT_1 <- 00 (wake). */
static void test_init_checks_id_then_wakes_chip(void)
{
    fake_i2c_set_reg(MPU, MPU6050_REG_WHO_AM_I, CAP_MPU6050_WHO_AM_I);
    fake_i2c_set_reg(MPU, MPU6050_REG_PWR_MGMT_1, 0x40);     /* reset value: SLEEP=1 */
    TEST_ASSERT_TRUE(MPU6050_Init());
    TEST_ASSERT_EQUAL_STRING("I2C R D0 75 1;I2C W D0 6B 00;", bus_log_str());
    TEST_ASSERT_EQUAL_HEX8(0x00, fake_i2c_reg(MPU, MPU6050_REG_PWR_MGMT_1));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "[MPU6050] WHO_AM_I: 0x68 (OK)"));
}

/* An MPU-6500 answers 0x70: the driver must not touch it. */
static void test_init_unexpected_id_does_not_write(void)
{
    fake_i2c_set_reg(MPU, MPU6050_REG_WHO_AM_I, 0x70);
    TEST_ASSERT_FALSE(MPU6050_Init());
    TEST_ASSERT_EQUAL_STRING("I2C R D0 75 1;", bus_log_str());
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "WHO_AM_I: 0x70 (Невірний ID, очікується 0x68)"));
}

static void test_init_nack_reports_error_and_does_not_write(void)
{
    fake_i2c_reset();                       /* nothing at 0x68 */
    TEST_ASSERT_FALSE(MPU6050_Init());
    TEST_ASSERT_EQUAL_STRING("I2C R D0 75 1;", bus_log_str());
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "[MPU6050] Помилка зчитування по I2C!"));
}

/* DEFECT-1 (fixed): if the wake-up write is NACKed the chip keeps sleeping,
 * so the driver must not report it as awake. */
static void test_init_failed_wake_write_is_reported(void)
{
    fake_i2c_set_reg(MPU, MPU6050_REG_WHO_AM_I, CAP_MPU6050_WHO_AM_I);
    fake_i2c_fail_reg(MPU, MPU6050_REG_PWR_MGMT_1);
    TEST_ASSERT_FALSE(MPU6050_Init());
    TEST_ASSERT_NULL(strstr(uart_out(), "Прокинуто"));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "Помилка запису PWR_MGMT_1"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_temperature_reproduces_datasheet_offset_point);
    RUN_TEST(test_temperature_zero_raw_is_36_53);
    RUN_TEST(test_temperature_from_capture_vectors);
    RUN_TEST(test_temperature_is_signed_16_bit);
    RUN_TEST(test_temperature_is_one_two_byte_read);
    RUN_TEST(test_temperature_nack_returns_false_and_keeps_old_value);
    RUN_TEST(test_temperature_missing_chip_returns_false);
    RUN_TEST(test_init_checks_id_then_wakes_chip);
    RUN_TEST(test_init_unexpected_id_does_not_write);
    RUN_TEST(test_init_nack_reports_error_and_does_not_write);
    RUN_TEST(test_init_failed_wake_write_is_reported);
    return UNITY_END();
}
