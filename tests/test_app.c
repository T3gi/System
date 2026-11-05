/* App: boot sequence and the super loop tasks of app.c over stubbed drivers.
 * damper.c and vision.c stay real, so a whole chain is checked: a temperature
 * or a picture goes in, the servo, the LED, the journal and the UART come out.
 * Tasks are static, so everything goes through App_Init() and App_Poll(). */
#include <string.h>
#include "unity.h"
#include "app.h"
#include "damper.h"
#include "vision.h"
#include "stub_app.h"
#include "perf.h"
#include "fake_time.h"
#include "bus_log.h"
#include "uart_capture.h"

extern volatile uint32_t btn_t_event;
extern volatile uint8_t  btn_pending;

#define BOOT_MS     1200u       /* App_Init waits for AEC/AWB */
#define BRIGHT      VISION_LIGHT_LEVEL
#define DARK        (VISION_DARK_LEVEL - 1u)
#define CAM_STEP    500u
#define STRIP_CALL  "CAM STRIP 704 0 8;"

void setUp(void)
{
    stub_app_reset();
    fake_time_reset();
    bus_log_reset();
    uart_reset();
    fake_time_freeze(0);
}

void tearDown(void) {}

/* App_Init, then clean logs: the clock stands at BOOT_MS, the moment 0 of a test. */
static void boot(void)
{
    App_Init();
    bus_log_reset();
    uart_reset();
}

/* One pass of the main loop, ms after the boot. */
static void poll_at(uint32_t ms)
{
    fake_time_freeze(BOOT_MS + ms);
    App_Poll();
}

static void clear_logs(void)
{
    bus_log_reset();
    uart_reset();
}

/* A good frame clears the error remembered by camera_task for the next test. */
static void end_camera_error(uint32_t ms)
{
    stub_cam_ret = 0;
    memset(&stub_cam_stats, 0, sizeof stub_cam_stats);
    stub_cam_stats.lines = 0xFFFFFFFFu;
    poll_at(ms);
}

/* Bright frame at 500 ms arms vision; two dark frames at 1000 and 1500 fire.
 * Returns the moment of the gesture, ms after the boot. */
static uint32_t gesture(void)
{
    stub_cam_luma = BRIGHT;
    poll_at(CAM_STEP);
    stub_cam_luma = DARK;
    poll_at(2 * CAM_STEP);
    poll_at(3 * CAM_STEP);
    stub_cam_luma = 50;
    return 3 * CAM_STEP;
}

/* --- boot ------------------------------------------------------------------ */

/* Must stay the first test: "no frame yet" exists only until the first capture
 * in this program.  0 would look like a dark picture.  No flash here, so that
 * the next test still sees the journal as never started. */
static void test_luma_is_a_dash_before_the_first_frame(void)
{
    stub_camera_ok = false;
    stub_flash_ok = false;
    boot();
    poll_at(1000);
    TEST_ASSERT_NOT_NULL(strstr(uart_out(),
        "TICK t=2200 rx=7 ore=1 drop=2 cmds=3 damper=CLOSED lock_s=0 lum=-\r\n"));
}

static void test_boot_order(void)
{
    App_Init();
    TEST_ASSERT_EQUAL_STRING("CONSOLE START;SELFTEST;SERVO 0;"
                             "JOURNAL INIT;JOURNAL PRINT 10;"
                             "CAM CONFIG 1;DELAY 1200;",
                             bus_log_str());
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "READY\r\n"));
    TEST_ASSERT_EQUAL_INT(DAMPER_CLOSED, Damper_State());
}

/* No camera: reported at boot, and the loop never tries to capture. */
static void test_missing_camera_is_reported_and_not_used(void)
{
    stub_camera_ok = false;
    App_Init();
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "ERR OV7670 not detected on SCCB\r\n"));
    TEST_ASSERT_NULL(strstr(bus_log_str(), "CAM CONFIG"));

    clear_logs();
    poll_at(5000);
    TEST_ASSERT_EQUAL_INT(0, bus_log_count("CAM STRIP"));
}

/* No flash: the rest works, without journal writes and without the blink.
 * Must stay the second test: App_Init runs once on the board and never turns
 * a started journal off, so no earlier test may have started it. */
static void test_missing_flash_keeps_the_rest_working(void)
{
    stub_flash_ok = false;
    App_Init();
    TEST_ASSERT_NULL(strstr(bus_log_str(), "JOURNAL"));

    clear_logs();
    stub_temp = 27.0f;
    poll_at(1000);
    TEST_ASSERT_EQUAL_INT(1, bus_log_count("SERVO 90;"));
    TEST_ASSERT_EQUAL_INT(0, bus_log_count("JOURNAL"));
    TEST_ASSERT_TRUE(stub_led);
}

/* --- the loop --------------------------------------------------------------- */

/* All tasks due at once: the order of the loop description (firmware-loop.md). */
static void test_tasks_run_in_the_documented_order(void)
{
    boot();
    poll_at(1000);
    TEST_ASSERT_EQUAL_STRING("CONSOLE;MPU;" STRIP_CALL, bus_log_str());
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "TICK t=2200 "));
}

/* FR-01: once a second, not earlier. */
static void test_temperature_is_read_once_a_second(void)
{
    boot();
    poll_at(999);
    TEST_ASSERT_EQUAL_INT(0, bus_log_count("MPU;"));
    poll_at(1000);
    TEST_ASSERT_EQUAL_INT(1, bus_log_count("MPU;"));
    poll_at(1999);
    TEST_ASSERT_EQUAL_INT(1, bus_log_count("MPU;"));
    poll_at(2000);
    TEST_ASSERT_EQUAL_INT(2, bus_log_count("MPU;"));
}

static void test_heartbeat_once_a_second_with_the_brightness(void)
{
    stub_cam_luma = 123;
    boot();
    poll_at(999);
    TEST_ASSERT_NULL(strstr(uart_out(), "TICK"));
    poll_at(1000);
    TEST_ASSERT_NOT_NULL(strstr(uart_out(),
        "TICK t=2200 rx=7 ore=1 drop=2 cmds=3 damper=CLOSED lock_s=0 lum=123\r\n"));
}

static void test_temperature_line_after_the_heartbeat(void)
{
    stub_temp = 25.5f;
    boot();
    poll_at(1000);
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "[MPU6050] Температура: 25.50 °C\r\n"));
}

static void test_negative_temperature_line(void)
{
    stub_temp = -1.5f;
    boot();
    poll_at(1000);
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "[MPU6050] Температура: -1.50 °C\r\n"));
}

/* --- temperature control ------------------------------------------------------ */

/* FR-02, FR-07, FR-08: servo, UART line and journal record of one change. */
static void test_heating_opens_the_damper(void)
{
    boot();
    stub_temp = 26.5f;
    poll_at(1000);
    TEST_ASSERT_EQUAL_INT(DAMPER_OPEN, Damper_State());
    TEST_ASSERT_EQUAL_INT(1, bus_log_count("SERVO 90;"));
    TEST_ASSERT_EQUAL_INT(1, bus_log_count("JOURNAL 1 1 2650 2200;"));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "DAMPER OPEN src=TEMP t=2200\r\n"));
}

/* FR-03. */
static void test_cooling_closes_the_damper(void)
{
    boot();
    stub_temp = 26.5f;
    poll_at(1000);
    clear_logs();

    stub_temp = 24.0f;
    poll_at(2000);
    TEST_ASSERT_EQUAL_INT(DAMPER_CLOSED, Damper_State());
    TEST_ASSERT_EQUAL_INT(1, bus_log_count("SERVO 0;"));
    TEST_ASSERT_EQUAL_INT(1, bus_log_count("JOURNAL 0 1 2400 3200;"));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "DAMPER CLOSED src=TEMP t=3200\r\n"));
}

/* Sensor failed: nothing moves on a stale value, and the error is visible. */
static void test_failed_sensor_read_changes_nothing(void)
{
    boot();
    stub_temp_ok = false;
    stub_temp = 30.0f;
    poll_at(1000);
    TEST_ASSERT_EQUAL_INT(DAMPER_CLOSED, Damper_State());
    TEST_ASSERT_EQUAL_INT(0, bus_log_count("SERVO"));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "Помилка отримання температури"));
}

static void test_failed_journal_write_is_reported(void)
{
    boot();
    stub_journal_append_ok = false;
    stub_temp = 26.5f;
    poll_at(1000);
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "ERR journal write\r\n"));
    TEST_ASSERT_EQUAL_INT(DAMPER_OPEN, Damper_State());
}

/* --- camera task ---------------------------------------------------------------- */

/* The fix of the camera timing: only 8 top lines, never a whole frame with the
 * interrupts off.  Line length follows the camera mode. */
static void test_capture_reads_a_short_strip(void)
{
    boot();
    poll_at(499);
    TEST_ASSERT_EQUAL_INT(0, bus_log_count("CAM STRIP"));
    poll_at(500);
    TEST_ASSERT_EQUAL_INT(1, bus_log_count(STRIP_CALL));
}

static void test_strip_length_follows_the_camera_mode(void)
{
    stub_cam_width = 640;
    boot();
    poll_at(500);
    TEST_ASSERT_EQUAL_INT(1, bus_log_count("CAM STRIP 1280 0 8;"));
    stub_cam_width = 352;
}

/* The second half of that fix: the pause counts from the END of a capture.  If it
 * counted from the start, the wait for a frame would use it up and the loop
 * would stand waiting for the next frame almost all the time. */
static void test_pause_counts_from_the_end_of_the_capture(void)
{
    boot();
    stub_cam_ms = 100;
    poll_at(500);                                   /* capture ends at 600 */
    TEST_ASSERT_EQUAL_INT(1, bus_log_count("CAM STRIP"));

    stub_cam_ms = 0;
    poll_at(1099);
    TEST_ASSERT_EQUAL_INT(1, bus_log_count("CAM STRIP"));
    poll_at(1100);
    TEST_ASSERT_EQUAL_INT(2, bus_log_count("CAM STRIP"));
}

/* U and V of the stub picture are bright: only the Y bytes may reach lum=. */
static void test_brightness_comes_from_the_picture(void)
{
    boot();
    stub_cam_luma = 77;
    poll_at(1000);
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "lum=77\r\n"));
}

/* --- gesture ------------------------------------------------------------------- */

/* FR-05, FR-07: the whole chain from a picture to the journal. */
static void test_gesture_opens_the_damper(void)
{
    boot();
    uint32_t t = gesture();
    TEST_ASSERT_EQUAL_INT(DAMPER_OPEN, Damper_State());
    TEST_ASSERT_EQUAL_INT(DAMPER_SRC_CAMERA, Damper_LastSource());
    TEST_ASSERT_EQUAL_INT(1, bus_log_count("SERVO 90;"));
    TEST_ASSERT_EQUAL_INT(1, bus_log_count("JOURNAL 1 2 2500 2700;"));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "DAMPER OPEN src=CAMERA t=2700\r\n"));
    TEST_ASSERT_EQUAL_UINT32(DAMPER_LOCK_MS, Damper_LockLeftMs(BOOT_MS + t));
}

/* A dark room at boot is not a gesture. */
static void test_dark_picture_after_boot_does_nothing(void)
{
    boot();
    stub_cam_luma = 0;
    for (uint32_t i = 1; i <= 20; i++) {
        poll_at(i * CAM_STEP);
    }
    TEST_ASSERT_EQUAL_INT(DAMPER_CLOSED, Damper_State());
    TEST_ASSERT_EQUAL_INT(0, bus_log_count("SERVO"));
}

/* A hand held on the lens toggles once. */
static void test_held_hand_toggles_once(void)
{
    boot();
    uint32_t t = gesture();
    stub_cam_luma = DARK;
    for (uint32_t i = 1; i <= 20; i++) {
        poll_at(t + i * CAM_STEP);
    }
    TEST_ASSERT_EQUAL_INT(1, bus_log_count("SERVO"));
    TEST_ASSERT_EQUAL_INT(DAMPER_OPEN, Damper_State());
}

/* FR-06: the heartbeat shows the lock, temperature cannot move the damper during
 * it and takes over at its end. */
static void test_gesture_locks_temperature_for_five_minutes(void)
{
    boot();
    uint32_t t = gesture();
    clear_logs();

    stub_temp = 23.0f;
    poll_at(t + DAMPER_LOCK_MS - 1000u);
    TEST_ASSERT_EQUAL_INT(DAMPER_OPEN, Damper_State());
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "damper=OPEN lock_s=1 "));

    poll_at(t + DAMPER_LOCK_MS);
    TEST_ASSERT_EQUAL_INT(DAMPER_CLOSED, Damper_State());
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "DAMPER CLOSED src=TEMP"));
}

/* --- broken frames ----------------------------------------------------------- */

/* Missing bytes are padded with zeros: such a frame is not a dark picture.  Two
 * in a row must not fire, must not change lum=, and are reported once. */
static void check_broken_frames_are_ignored(ov7670_capture_stats_t broken)
{
    stub_cam_luma = BRIGHT;
    boot();
    poll_at(CAM_STEP);                              /* armed */
    clear_logs();

    stub_cam_stats = broken;
    stub_cam_luma = 0;
    poll_at(2 * CAM_STEP);
    poll_at(3 * CAM_STEP);
    poll_at(4 * CAM_STEP);

    TEST_ASSERT_EQUAL_INT(DAMPER_CLOSED, Damper_State());
    TEST_ASSERT_EQUAL_INT(0, bus_log_count("SERVO"));
    const char *first = strstr(uart_out(), "ERR camera frame ret=-7 mode=CIF\r\n");
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NULL(strstr(first + 1, "ERR camera frame"));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "lum=-\r\n"));

    end_camera_error(5 * CAM_STEP);
}

static void test_frame_with_short_lines_is_ignored(void)
{
    ov7670_capture_stats_t broken = stub_cam_stats;
    broken.short_lines = 1;
    check_broken_frames_are_ignored(broken);
}

static void test_frame_with_dropouts_is_ignored(void)
{
    ov7670_capture_stats_t broken = stub_cam_stats;
    broken.dropouts = 1;
    check_broken_frames_are_ignored(broken);
}

static void test_frame_with_missing_lines_is_ignored(void)
{
    ov7670_capture_stats_t broken = stub_cam_stats;
    broken.lines = 7;
    check_broken_frames_are_ignored(broken);
}

/* --- capture errors ------------------------------------------------------------ */

/* No VSYNC (ret -1): printed once, not twice a second, and lum= shows no frame. */
static void test_capture_error_is_printed_once(void)
{
    boot();
    stub_cam_ret = -1;
    poll_at(CAM_STEP);
    poll_at(2 * CAM_STEP);
    poll_at(3 * CAM_STEP);

    const char *first = strstr(uart_out(), "ERR camera frame ret=-1 mode=CIF\r\n");
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NULL(strstr(first + 1, "ERR camera frame"));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "lum=-\r\n"));

    end_camera_error(4 * CAM_STEP);
}

static void test_another_error_is_printed_too(void)
{
    boot();
    stub_cam_ret = -1;
    poll_at(CAM_STEP);
    stub_cam_ret = -4;
    poll_at(2 * CAM_STEP);

    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "ret=-1 "));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "ret=-4 "));

    end_camera_error(3 * CAM_STEP);
}

/* The camera came back: lum= is real again, and a new failure is printed again. */
static void test_recovery_after_an_error(void)
{
    boot();
    stub_cam_ret = -1;
    poll_at(CAM_STEP);
    end_camera_error(2 * CAM_STEP);
    clear_logs();

    stub_cam_luma = 66;
    poll_at(3 * CAM_STEP);
    poll_at(4 * CAM_STEP);
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "lum=66\r\n"));

    clear_logs();
    stub_cam_ret = -1;
    poll_at(5 * CAM_STEP);
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "ERR camera frame ret=-1"));

    end_camera_error(6 * CAM_STEP);
}

/* --- LED ----------------------------------------------------------------------- */

/* FR-08: on while open; after a journal write 4 phases of 500 ms: opposite, new
 * state, opposite, new state; then the state again. */
static void test_led_blinks_twice_then_shows_open(void)
{
    boot();
    poll_at(1);
    TEST_ASSERT_FALSE(stub_led);

    stub_temp = 26.5f;
    poll_at(1000);
    TEST_ASSERT_FALSE(stub_led);
    poll_at(1499);
    TEST_ASSERT_FALSE(stub_led);
    poll_at(1500);
    TEST_ASSERT_TRUE(stub_led);
    poll_at(2000);
    TEST_ASSERT_FALSE(stub_led);
    poll_at(2500);
    TEST_ASSERT_TRUE(stub_led);
    poll_at(3000);
    TEST_ASSERT_TRUE(stub_led);
    poll_at(3490);
    TEST_ASSERT_TRUE(stub_led);
}

static void test_led_blinks_twice_then_shows_closed(void)
{
    boot();
    stub_temp = 26.5f;
    poll_at(1000);
    poll_at(3100);                  /* blink over, last temperature read */

    stub_temp = 24.0f;
    poll_at(4100);
    TEST_ASSERT_TRUE(stub_led);
    poll_at(4600);
    TEST_ASSERT_FALSE(stub_led);
    poll_at(5100);
    TEST_ASSERT_TRUE(stub_led);
    poll_at(5600);
    TEST_ASSERT_FALSE(stub_led);
    poll_at(6100);
    TEST_ASSERT_FALSE(stub_led);
}

/* --- button -------------------------------------------------------------------- */

/* One frame per press: holding the button does not repeat it. */
static void test_button_captures_once_per_press(void)
{
    boot();
    stub_button = true;
    poll_at(1);
    poll_at(2);
    poll_at(3);
    TEST_ASSERT_EQUAL_INT(1, bus_log_count("BUTTON FRAME;"));

    stub_button = false;
    poll_at(4);
    stub_button = true;
    poll_at(5);
    TEST_ASSERT_EQUAL_INT(2, bus_log_count("BUTTON FRAME;"));
}

/* The event from EXTI15_10 (timing logs of the measurements part). */
static void test_button_event_prints_the_latency_once(void)
{
    boot();
    btn_t_event = 84000u;
    fake_cycles = 84840u;
    btn_pending = 1;

    poll_at(1);
    TEST_ASSERT_EQUAL_UINT8(0, btn_pending);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)GPIO_PIN_9 << 16U, fake_gpioc.BSRR);
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "EVT t_us=1000 name=btn_press\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(uart_out(), "EVT t_us=1010 name=capture_start latency_us=10\r\n"));

    clear_logs();
    poll_at(2);
    TEST_ASSERT_NULL(strstr(uart_out(), "EVT"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_luma_is_a_dash_before_the_first_frame);   /* must stay first  */
    RUN_TEST(test_missing_flash_keeps_the_rest_working);    /* must stay second */
    RUN_TEST(test_boot_order);
    RUN_TEST(test_missing_camera_is_reported_and_not_used);
    RUN_TEST(test_tasks_run_in_the_documented_order);
    RUN_TEST(test_temperature_is_read_once_a_second);
    RUN_TEST(test_heartbeat_once_a_second_with_the_brightness);
    RUN_TEST(test_temperature_line_after_the_heartbeat);
    RUN_TEST(test_negative_temperature_line);
    RUN_TEST(test_heating_opens_the_damper);
    RUN_TEST(test_cooling_closes_the_damper);
    RUN_TEST(test_failed_sensor_read_changes_nothing);
    RUN_TEST(test_failed_journal_write_is_reported);
    RUN_TEST(test_capture_reads_a_short_strip);
    RUN_TEST(test_strip_length_follows_the_camera_mode);
    RUN_TEST(test_pause_counts_from_the_end_of_the_capture);
    RUN_TEST(test_brightness_comes_from_the_picture);
    RUN_TEST(test_gesture_opens_the_damper);
    RUN_TEST(test_dark_picture_after_boot_does_nothing);
    RUN_TEST(test_held_hand_toggles_once);
    RUN_TEST(test_gesture_locks_temperature_for_five_minutes);
    RUN_TEST(test_frame_with_short_lines_is_ignored);
    RUN_TEST(test_frame_with_dropouts_is_ignored);
    RUN_TEST(test_frame_with_missing_lines_is_ignored);
    RUN_TEST(test_capture_error_is_printed_once);
    RUN_TEST(test_another_error_is_printed_too);
    RUN_TEST(test_recovery_after_an_error);
    RUN_TEST(test_led_blinks_twice_then_shows_open);
    RUN_TEST(test_led_blinks_twice_then_shows_closed);
    RUN_TEST(test_button_captures_once_per_press);
    RUN_TEST(test_button_event_prints_the_latency_once);
    return UNITY_END();
}
