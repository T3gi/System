/* App: "hand covers the lens" detector (FR-05).  Pure logic, no doubles. */
#include <string.h>
#include "unity.h"
#include "vision.h"

#define BRIGHT   VISION_LIGHT_LEVEL
#define DARK     (VISION_DARK_LEVEL - 1u)
#define MIDDLE   ((VISION_DARK_LEVEL + VISION_LIGHT_LEVEL) / 2u)

void setUp(void)
{
    Vision_Reset();
}

void tearDown(void) {}

/* Feed n frames; returns how many of them fired the trigger. */
static int feed(uint8_t luma, int n)
{
    int fired = 0;
    for (int i = 0; i < n; i++) {
        if (Vision_Update(luma)) fired++;
    }
    return fired;
}

/* --- mean luma --------------------------------------------------------- */

/* UYVY: Y is every second byte starting at index 1, U and V do not count. */
static void test_mean_luma_uses_only_the_y_bytes(void)
{
    const uint8_t frame[] = { 255, 10, 255, 20, 255, 30, 255, 40 };
    TEST_ASSERT_EQUAL_UINT8(25, Vision_MeanLuma(frame, sizeof frame));
}

static void test_mean_luma_of_no_data_is_zero(void)
{
    const uint8_t one_byte[] = { 200 };
    TEST_ASSERT_EQUAL_UINT8(0, Vision_MeanLuma(one_byte, 0));
    TEST_ASSERT_EQUAL_UINT8(0, Vision_MeanLuma(one_byte, 1));
}

/* The largest strip of 32 VGA lines, all white: the sum must not overflow. */
static void test_mean_luma_of_a_white_strip(void)
{
    static uint8_t strip[1312u * 32u];
    memset(strip, 255, sizeof strip);
    TEST_ASSERT_EQUAL_UINT8(255, Vision_MeanLuma(strip, sizeof strip));
}

/* --- trigger ----------------------------------------------------------- */

/* A dark room at boot is not a gesture: nothing fires before the first bright frame. */
static void test_dark_frames_at_boot_do_not_fire(void)
{
    TEST_ASSERT_EQUAL_INT(0, feed(0, 20));
}

/* Arming needs a really bright frame, and only then does darkness count. */
static void test_arms_at_the_light_level(void)
{
    TEST_ASSERT_EQUAL_INT(0, feed(VISION_LIGHT_LEVEL - 1u, 1));
    TEST_ASSERT_EQUAL_INT(0, feed(DARK, 5));

    TEST_ASSERT_EQUAL_INT(0, feed(BRIGHT, 1));
    TEST_ASSERT_EQUAL_INT(1, feed(DARK, VISION_DARK_FRAMES));
}

/* One dark frame is not enough, the last of VISION_DARK_FRAMES fires. */
static void test_fires_on_the_last_required_dark_frame(void)
{
    feed(BRIGHT, 1);
    TEST_ASSERT_EQUAL_INT(0, feed(DARK, VISION_DARK_FRAMES - 1u));
    TEST_ASSERT_TRUE(Vision_Update(DARK));
}

/* "Below VISION_DARK_LEVEL": exactly that level is not dark. */
static void test_dark_level_itself_is_not_dark(void)
{
    feed(BRIGHT, 1);
    TEST_ASSERT_EQUAL_INT(0, feed(VISION_DARK_LEVEL, 10));
}

/* The dark frames must go in a row: a brighter frame starts the count again. */
static void test_a_brighter_frame_resets_the_dark_count(void)
{
    feed(BRIGHT, 1);
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(0, feed(DARK, VISION_DARK_FRAMES - 1u));
        TEST_ASSERT_EQUAL_INT(0, feed(MIDDLE, 1));
    }
}

/* A hand held on the lens toggles the damper once, not every 2 frames. */
static void test_held_hand_fires_only_once(void)
{
    feed(BRIGHT, 1);
    TEST_ASSERT_EQUAL_INT(1, feed(DARK, 50));
}

/* After firing, a half-lit frame does not re-arm, a bright one does. */
static void test_rearms_only_after_a_bright_frame(void)
{
    feed(BRIGHT, 1);
    feed(DARK, VISION_DARK_FRAMES);

    feed(MIDDLE, 1);
    TEST_ASSERT_EQUAL_INT(0, feed(DARK, 10));

    feed(BRIGHT, 1);
    TEST_ASSERT_EQUAL_INT(1, feed(DARK, 10));
}

/* The next gesture needs all its dark frames again, not just one. */
static void test_rearming_starts_the_dark_count_again(void)
{
    feed(BRIGHT, 1);
    feed(DARK, VISION_DARK_FRAMES);
    feed(BRIGHT, 1);

    TEST_ASSERT_EQUAL_INT(0, feed(DARK, VISION_DARK_FRAMES - 1u));
    TEST_ASSERT_TRUE(Vision_Update(DARK));
}

/* Reset drops an armed state and a partial dark count. */
static void test_reset_disarms(void)
{
    feed(BRIGHT, 1);
    feed(DARK, VISION_DARK_FRAMES - 1u);
    Vision_Reset();
    TEST_ASSERT_EQUAL_INT(0, feed(DARK, 10));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mean_luma_uses_only_the_y_bytes);
    RUN_TEST(test_mean_luma_of_no_data_is_zero);
    RUN_TEST(test_mean_luma_of_a_white_strip);
    RUN_TEST(test_dark_frames_at_boot_do_not_fire);
    RUN_TEST(test_arms_at_the_light_level);
    RUN_TEST(test_fires_on_the_last_required_dark_frame);
    RUN_TEST(test_dark_level_itself_is_not_dark);
    RUN_TEST(test_a_brighter_frame_resets_the_dark_count);
    RUN_TEST(test_held_hand_fires_only_once);
    RUN_TEST(test_rearms_only_after_a_bright_frame);
    RUN_TEST(test_rearming_starts_the_dark_count_again);
    RUN_TEST(test_reset_disarms);
    return UNITY_END();
}
