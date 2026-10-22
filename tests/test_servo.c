/* SG92R servo: timer setup and angle -> pulse width. */
#include "unity.h"
#include "servo.h"
#include "fake_pwm.h"
#include "bus_log.h"

#define TIM3_CLOCK_HZ 84000000.0     /* APB1 x2, see analysis.md §6 */

void setUp(void)
{
    fake_pwm_reset();
    bus_log_reset();
}

void tearDown(void) {}

static unsigned pulse_for(uint8_t angle)
{
    Servo_SetAngle(angle);
    return fake_pwm_pulse();
}

/* 1 us tick and 20 ms (50 Hz) period, then the neutral 1.5 ms pulse. */
static void test_init_sets_1us_tick_and_20ms_period(void)
{
    Servo_Init();
    TEST_ASSERT_EQUAL_STRING("PWMINIT 83 19999;PWM 1500;", bus_log_str());
    double tick_us = (fake_pwm_prescaler() + 1) / TIM3_CLOCK_HZ * 1e6;
    double period_ms = (fake_pwm_period() + 1) * tick_us / 1000.0;
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, tick_us);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 20.0, period_ms);
}

static void test_angle_maps_to_1_to_2_ms(void)
{
    TEST_ASSERT_EQUAL_UINT(1000, pulse_for(0));
    TEST_ASSERT_EQUAL_UINT(1250, pulse_for(45));
    TEST_ASSERT_EQUAL_UINT(1500, pulse_for(90));
    TEST_ASSERT_EQUAL_UINT(2000, pulse_for(180));
}

/* Integer division truncates: 1° = 1005.55 us -> 1005. */
static void test_intermediate_angles_truncate(void)
{
    TEST_ASSERT_EQUAL_UINT(1005, pulse_for(1));
    TEST_ASSERT_EQUAL_UINT(1994, pulse_for(179));
}

static void test_angle_above_180_is_clamped(void)
{
    TEST_ASSERT_EQUAL_UINT(2000, pulse_for(181));
    TEST_ASSERT_EQUAL_UINT(2000, pulse_for(255));
}

static void test_pulse_grows_with_angle_and_stays_in_range(void)
{
    unsigned prev = 0;
    for (unsigned a = 0; a <= 180; a++) {
        unsigned p = pulse_for((uint8_t)a);
        TEST_ASSERT_TRUE(p >= prev);
        TEST_ASSERT_UINT_WITHIN(500, 1500, p);
        prev = p;
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_sets_1us_tick_and_20ms_period);
    RUN_TEST(test_angle_maps_to_1_to_2_ms);
    RUN_TEST(test_intermediate_angles_truncate);
    RUN_TEST(test_angle_above_180_is_clamped);
    RUN_TEST(test_pulse_grows_with_angle_and_stays_in_range);
    return UNITY_END();
}
