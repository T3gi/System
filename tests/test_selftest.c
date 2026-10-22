/* App: boot self-test over a stubbed Devices layer. */
#include <string.h>
#include "unity.h"
#include "selftest.h"
#include "stub_devices.h"
#include "fake_time.h"
#include "bus_log.h"
#include "uart_capture.h"

void setUp(void)
{
    stub_devices_reset();
    fake_time_reset();
    bus_log_reset();
    uart_reset();
}

void tearDown(void) {}

/* Same order as before the move to App: I2C sensors first, the camera last,
 * because SCCB takes over the I2C1 pins (PB8/PB9). */
static void test_devices_are_checked_in_boot_order(void)
{
    SelfTest_Run();
    TEST_ASSERT_EQUAL_STRING("SCAN;SERVO INIT;"
                             "SERVO 0;DELAY 1000;SERVO 90;DELAY 1000;SERVO 180;DELAY 1000;"
                             "W25Q64 ID;MPU6050 INIT;GY89 ID;CAM POWERUP;CAM PROBE;",
                             bus_log_str());
}

static void test_all_devices_ok(void)
{
    selftest_result_t r = SelfTest_Run();
    TEST_ASSERT_TRUE(r.w25q64 && r.mpu6050 && r.gy89 && r.ov7670);
    TEST_ASSERT_EQUAL_INT(4, r.passed);
    TEST_ASSERT_EQUAL_INT(4, r.total);
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "SELFTEST W25Q64 OK\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "SELFTEST OV7670 OK\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "SELFTEST SUMMARY 4/4 OK\r\n"));
}

/* A disconnected GY-89 is reported as FAIL and counted in the summary. */
static void test_missing_gy89_is_reported_as_fail(void)
{
    stub_gy89_ok = false;
    selftest_result_t r = SelfTest_Run();
    TEST_ASSERT_FALSE(r.gy89);
    TEST_ASSERT_TRUE(r.w25q64 && r.mpu6050 && r.ov7670);
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "SELFTEST GY89 FAIL\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "SELFTEST SUMMARY 3/4 OK\r\n"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_devices_are_checked_in_boot_order);
    RUN_TEST(test_all_devices_ok);
    RUN_TEST(test_missing_gy89_is_reported_as_fail);
    return UNITY_END();
}
