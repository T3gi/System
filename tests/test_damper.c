/* App: damper state machine (FR-02, FR-03, FR-05, FR-06).  Pure logic, no doubles. */
#include "unity.h"
#include "damper.h"

#define ABOVE_OPEN   (DAMPER_OPEN_ABOVE_C100 + 1)
#define AT_CLOSE     DAMPER_CLOSE_BELOW_C100
#define BETWEEN      2500

void setUp(void)
{
    Damper_Init();
}

void tearDown(void) {}

/* --- temperature control ----------------------------------------------- */

static void test_starts_closed_without_lock_or_source(void)
{
    TEST_ASSERT_EQUAL_INT(DAMPER_CLOSED, Damper_State());
    TEST_ASSERT_EQUAL_INT(DAMPER_SRC_NONE, Damper_LastSource());
    TEST_ASSERT_EQUAL_UINT32(0, Damper_LockLeftMs(0));
}

/* FR-02: "exceeds the threshold": exactly 26.00 C does not open yet. */
static void test_opens_only_above_the_threshold(void)
{
    TEST_ASSERT_FALSE(Damper_OnTemperature(DAMPER_OPEN_ABOVE_C100, 1000));
    TEST_ASSERT_EQUAL_INT(DAMPER_CLOSED, Damper_State());

    TEST_ASSERT_TRUE(Damper_OnTemperature(ABOVE_OPEN, 2000));
    TEST_ASSERT_EQUAL_INT(DAMPER_OPEN, Damper_State());
    TEST_ASSERT_EQUAL_INT(DAMPER_SRC_TEMP, Damper_LastSource());
}

/* FR-03: closes at 24.00 C, not before. */
static void test_closes_only_at_the_lower_threshold(void)
{
    Damper_OnTemperature(ABOVE_OPEN, 1000);

    TEST_ASSERT_FALSE(Damper_OnTemperature(AT_CLOSE + 1, 2000));
    TEST_ASSERT_EQUAL_INT(DAMPER_OPEN, Damper_State());

    TEST_ASSERT_TRUE(Damper_OnTemperature(AT_CLOSE, 3000));
    TEST_ASSERT_EQUAL_INT(DAMPER_CLOSED, Damper_State());
    TEST_ASSERT_EQUAL_INT(DAMPER_SRC_TEMP, Damper_LastSource());
}

/* Between the thresholds the state stays whatever it was. */
static void test_hysteresis_keeps_the_state_between_thresholds(void)
{
    TEST_ASSERT_FALSE(Damper_OnTemperature(BETWEEN, 1000));
    TEST_ASSERT_EQUAL_INT(DAMPER_CLOSED, Damper_State());

    Damper_OnTemperature(ABOVE_OPEN, 2000);
    TEST_ASSERT_FALSE(Damper_OnTemperature(BETWEEN, 3000));
    TEST_ASSERT_EQUAL_INT(DAMPER_OPEN, Damper_State());
}

/* A change is reported once, so the journal gets one record per change. */
static void test_a_change_is_reported_only_once(void)
{
    TEST_ASSERT_TRUE(Damper_OnTemperature(ABOVE_OPEN, 1000));
    TEST_ASSERT_FALSE(Damper_OnTemperature(ABOVE_OPEN + 100, 2000));

    TEST_ASSERT_TRUE(Damper_OnTemperature(AT_CLOSE, 3000));
    TEST_ASSERT_FALSE(Damper_OnTemperature(AT_CLOSE - 100, 4000));
}

/* Temperature control alone never starts the lock. */
static void test_temperature_change_does_not_lock(void)
{
    Damper_OnTemperature(ABOVE_OPEN, 1000);
    TEST_ASSERT_EQUAL_UINT32(0, Damper_LockLeftMs(1000));
}

/* --- camera override and lock ------------------------------------------ */

/* FR-05: the gesture toggles the state in both directions. */
static void test_camera_trigger_toggles_the_state(void)
{
    TEST_ASSERT_TRUE(Damper_OnCameraTrigger(1000));
    TEST_ASSERT_EQUAL_INT(DAMPER_OPEN, Damper_State());
    TEST_ASSERT_EQUAL_INT(DAMPER_SRC_CAMERA, Damper_LastSource());

    TEST_ASSERT_TRUE(Damper_OnCameraTrigger(2000));
    TEST_ASSERT_EQUAL_INT(DAMPER_CLOSED, Damper_State());
    TEST_ASSERT_EQUAL_INT(DAMPER_SRC_CAMERA, Damper_LastSource());
}

/* FR-06: temperature is ignored for 5 minutes, in both directions. */
static void test_lock_blocks_temperature_control(void)
{
    Damper_OnCameraTrigger(1000);                       /* open, locked */
    TEST_ASSERT_FALSE(Damper_OnTemperature(AT_CLOSE, 2000));
    TEST_ASSERT_EQUAL_INT(DAMPER_OPEN, Damper_State());

    Damper_OnCameraTrigger(3000);                       /* closed, locked */
    TEST_ASSERT_FALSE(Damper_OnTemperature(ABOVE_OPEN, 4000));
    TEST_ASSERT_EQUAL_INT(DAMPER_CLOSED, Damper_State());
    TEST_ASSERT_EQUAL_INT(DAMPER_SRC_CAMERA, Damper_LastSource());
}

/* The lock counts down and lasts exactly DAMPER_LOCK_MS. */
static void test_lock_time_left_and_exact_end(void)
{
    const uint32_t t0 = 5000;
    Damper_OnCameraTrigger(t0);

    TEST_ASSERT_EQUAL_UINT32(DAMPER_LOCK_MS, Damper_LockLeftMs(t0));
    TEST_ASSERT_EQUAL_UINT32(1, Damper_LockLeftMs(t0 + DAMPER_LOCK_MS - 1));
    TEST_ASSERT_EQUAL_UINT32(0, Damper_LockLeftMs(t0 + DAMPER_LOCK_MS));
}

/* One millisecond before the end still locked, at the end control is back. */
static void test_temperature_control_returns_after_the_lock(void)
{
    const uint32_t t0 = 5000;
    Damper_OnCameraTrigger(t0);                         /* open */

    TEST_ASSERT_FALSE(Damper_OnTemperature(AT_CLOSE, t0 + DAMPER_LOCK_MS - 1));
    TEST_ASSERT_TRUE(Damper_OnTemperature(AT_CLOSE, t0 + DAMPER_LOCK_MS));
    TEST_ASSERT_EQUAL_INT(DAMPER_CLOSED, Damper_State());
    TEST_ASSERT_EQUAL_INT(DAMPER_SRC_TEMP, Damper_LastSource());
}

/* A second gesture during the lock starts the 5 minutes again. */
static void test_second_gesture_restarts_the_lock(void)
{
    Damper_OnCameraTrigger(1000);
    Damper_OnCameraTrigger(61000);
    TEST_ASSERT_EQUAL_UINT32(DAMPER_LOCK_MS, Damper_LockLeftMs(61000));
    TEST_ASSERT_EQUAL_UINT32(1, Damper_LockLeftMs(61000 + DAMPER_LOCK_MS - 1));
}

/* GetTick wraps after ~49.7 days; the lock must survive it. */
static void test_lock_survives_tick_wraparound(void)
{
    const uint32_t t0 = 0xFFFFFF00u;
    Damper_OnCameraTrigger(t0);
    TEST_ASSERT_EQUAL_UINT32(DAMPER_LOCK_MS - 0x200u, Damper_LockLeftMs(t0 + 0x200u));
    TEST_ASSERT_EQUAL_UINT32(0, Damper_LockLeftMs(t0 + DAMPER_LOCK_MS));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_starts_closed_without_lock_or_source);
    RUN_TEST(test_opens_only_above_the_threshold);
    RUN_TEST(test_closes_only_at_the_lower_threshold);
    RUN_TEST(test_hysteresis_keeps_the_state_between_thresholds);
    RUN_TEST(test_a_change_is_reported_only_once);
    RUN_TEST(test_temperature_change_does_not_lock);
    RUN_TEST(test_camera_trigger_toggles_the_state);
    RUN_TEST(test_lock_blocks_temperature_control);
    RUN_TEST(test_lock_time_left_and_exact_end);
    RUN_TEST(test_temperature_control_returns_after_the_lock);
    RUN_TEST(test_second_gesture_restarts_the_lock);
    RUN_TEST(test_lock_survives_tick_wraparound);
    return UNITY_END();
}
