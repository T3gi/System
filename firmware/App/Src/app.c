/**
 * @file  app.c
 * @brief Boot sequence and main loop, moved from main.c.
 *
 * Super loop: App_Poll() calls the tasks one after another.  A periodic task
 * first checks whether its period has passed and returns at once if not.
 */
#include "app.h"
#include "perf.h"
#include "selftest.h"
#include "console.h"
#include "damper.h"
#include "vision.h"
#include "journal.h"
#include "ov7670.h"
#include "ov7670_capture.h"
#include "mpu6050.h"
#include "servo.h"
#include "platform_time.h"
#include "platform_board.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define TEMP_PERIOD_MS     1000    /* FR-01: read the temperature once a second */
#define CAMERA_PERIOD_MS    500    /* FR-04: pause between captures; a CIF frame
                                    * comes every ~601 ms, so one capture per frame */
#define CAMERA_STRIP_LINES    8    /* lines used for brightness: ~16 ms with IRQs off */
#define CAMERA_ERR_BROKEN    (-7)  /* lines missing or short: not a real picture */
#define TICK_PERIOD_MS     1000    /* heartbeat line */

#define DAMPER_CLOSED_DEG     0    /* servo angle of a closed damper */
#define DAMPER_OPEN_DEG      90    /* FR-02: open = 90 degrees */

#define LED_BLINK_MS        500    /* FR-08: one blink phase after a journal write */
#define JOURNAL_BOOT_PRINT   10    /* PRD test 3: records printed at boot */

static uint32_t s_last_tick;
static uint32_t s_last_temp;
static uint32_t s_last_camera;
static bool     s_btn_prev;

static float    s_temp;            /* last temperature */
static bool     s_temp_ok;         /* last read succeeded */
static bool     s_camera_ok;       /* camera answered at boot */
static int      s_camera_err;      /* last snapshot error, 0 = none */
static uint8_t  s_luma;            /* brightness of the last frame */
static bool     s_luma_ok;         /* the last capture gave a real frame */
static bool     s_journal_ok;
static uint32_t s_blink_start;
static bool     s_blinking;

volatile uint32_t btn_t_event = 0;
volatile uint8_t btn_pending = 0;
perf_stat_t st_btn_latency = { .name = "btn_latency" };

void App_Init(void)
{
    Platform_Console_Start();

    selftest_result_t st = SelfTest_Run();

    /* The self-test sweep leaves the servo at 180 degrees; start closed. */
    Damper_Init();
    Servo_SetAngle(DAMPER_CLOSED_DEG);

    if (st.w25q64 && Journal_Init()) {
        s_journal_ok = true;
        Journal_PrintLast(JOURNAL_BOOT_PRINT);
    }

    s_camera_ok = st.ov7670;
    if (st.ov7670) {
        ov7670_configure(ov7670_current_mode());
        Platform_DelayMs(1200);            /* AWB/AEC convergence */
    } else {
        printf("ERR OV7670 not detected on SCCB\r\n");
    }
    Vision_Reset();

    printf("READY\r\n");
    Console_Help();
    printf("OK\r\n");

    s_last_tick   = Platform_GetTick();
    s_last_temp   = Platform_GetTick();
    s_last_camera = Platform_GetTick();
    s_btn_prev    = false;
}

static void print_temperature(void)
{
    if (s_temp_ok) {
        int32_t int_part = (int32_t)s_temp;
        // Отримуємо 2 знаки після коми (множимо на 100)
        int32_t frac_part = (int32_t)((s_temp - (float)int_part) * 100.0f);

        // Враховуємо випадок від'ємних температур
        if (frac_part < 0) {
            frac_part = -frac_part;
        }

        printf("[MPU6050] Температура: %ld.%02ld °C\r\n", (long)int_part, (long)frac_part);
    } else {
        printf("[MPU6050] Помилка отримання температури!\r\n");
    }
}

/* The damper logic has just changed state: move the servo, report it and
 * write it to the journal (FR-07). */
static void damper_changed(void)
{
    const char *state = "CLOSED";
    const char *source = "TEMP";

    if (Damper_State() == DAMPER_OPEN) {
        Servo_SetAngle(DAMPER_OPEN_DEG);
        state = "OPEN";
    } else {
        Servo_SetAngle(DAMPER_CLOSED_DEG);
    }
    if (Damper_LastSource() == DAMPER_SRC_CAMERA) {
        source = "CAMERA";
    }
    printf("DAMPER %s src=%s t=%lu\r\n", state, source, (unsigned long)Platform_GetTick());

    if (s_journal_ok) {
        int16_t temp_c100 = (int16_t)(s_temp * 100.0f);
        if (!Journal_Append(Damper_State(), Damper_LastSource(), temp_c100, Platform_GetTick())) {
            printf("ERR journal write\r\n");
        }
        s_blinking = true;
        s_blink_start = Platform_GetTick();
    }
}

/* USER button still works as a manual trigger: grab a full frame of the
 * current mode using as many strips as needed. */
static void button_task(void)
{
    bool btn = Platform_ButtonPressed();
    if (btn && !s_btn_prev) {
        Console_CaptureFrame();
    }
    s_btn_prev = btn;
}

/* FR-01: read the temperature.  FR-02, FR-03: open or close the damper. */
static void temp_task(void)
{
    if (Platform_GetTick() - s_last_temp < TEMP_PERIOD_MS) {
        return;
    }
    s_last_temp = Platform_GetTick();

    s_temp_ok = MPU6050_ReadTemperature(&s_temp);
    if (!s_temp_ok) {
        return;
    }
    int32_t temp_c100 = (int32_t)(s_temp * 100.0f);
    if (Damper_OnTemperature(temp_c100, Platform_GetTick())) {
        damper_changed();
    }
}

/* FR-04: take a frame.  FR-05: a hand over the lens toggles the damper
 * (and starts the FR-06 lock inside Damper_OnCameraTrigger).
 *
 * Only the top lines of the frame are read: the capture runs with interrupts
 * off, and a whole frame (~570 ms) would stop the system time and the UART.
 * The time stamp is taken after the capture, so the next call comes shortly
 * before the next frame instead of waiting for it most of the time. */
static void camera_task(void)
{
    if (!s_camera_ok) {
        return;
    }
    if (Platform_GetTick() - s_last_camera < CAMERA_PERIOD_MS) {
        return;
    }

    const ov7670_mode_t *m = ov7670_mode_info(ov7670_current_mode());
    int got = OV7670_CaptureStrip((uint32_t)m->width * 2u, 0, CAMERA_STRIP_LINES);
    s_last_camera = Platform_GetTick();

    if (got >= 0) {
        /* Missing bytes are padded with zeros, which would look like a hand. */
        ov7670_capture_stats_t st = OV7670_LastCapture();
        if (st.lines != CAMERA_STRIP_LINES || st.dropouts != 0 || st.short_lines != 0) {
            got = CAMERA_ERR_BROKEN;
        }
    }
    if (got < 0) {
        s_luma_ok = false;
        /* Print the error once, not twice a second. */
        if (got != s_camera_err) {
            printf("ERR camera frame ret=%d mode=%s\r\n", got, m->name);
        }
        s_camera_err = got;
        return;
    }
    s_camera_err = 0;

    s_luma = Vision_MeanLuma(OV7670_Buffer(), (uint32_t)got);
    s_luma_ok = true;
    if (Vision_Update(s_luma)) {
        Damper_OnCameraTrigger(Platform_GetTick());
        damper_changed();
    }
}

/* FR-08: LED on while the damper is open.  After a journal write it blinks
 * twice (4 phases of LED_BLINK_MS: opposite, new state, opposite, new state)
 * and then shows the new state. */
static void led_task(void)
{
    bool open = (Damper_State() == DAMPER_OPEN);

    if (s_blinking && Platform_GetTick() - s_blink_start >= 4 * LED_BLINK_MS) {
        s_blinking = false;
    }
    if (s_blinking) {
        uint32_t phase = (Platform_GetTick() - s_blink_start) / LED_BLINK_MS;
        if (phase % 2 == 0) {
            Platform_LedSet(!open);
        } else {
            Platform_LedSet(open);
        }
    } else {
        Platform_LedSet(open);
    }
}

/* Idle heartbeat.  If these stop while the board is otherwise silent, the
 * main loop itself is stuck; if they continue but commands go unanswered,
 * the RX path is at fault. */
static void tick_task(void)
{
    if (Platform_GetTick() - s_last_tick < TICK_PERIOD_MS) {
        return;
    }
    s_last_tick = Platform_GetTick();

    const char *state = "CLOSED";
    if (Damper_State() == DAMPER_OPEN) {
        state = "OPEN";
    }
    printf("TICK t=%lu rx=%lu ore=%lu drop=%lu cmds=%lu damper=%s lock_s=%lu lum=",
           (unsigned long)s_last_tick,
           (unsigned long)Platform_Console_RxBytes(),
           (unsigned long)Platform_Console_RxOverruns(),
           (unsigned long)Platform_Console_RxDropped(),
           (unsigned long)Console_CommandCount(),
           state,
           (unsigned long)(Damper_LockLeftMs(s_last_tick) / 1000));
    if (s_luma_ok) {
        printf("%u\r\n", (unsigned)s_luma);
    } else {
        printf("-\r\n");       /* no frame yet: 0 would look like a dark picture */
    }
    print_temperature();
}

void Process_Button_Event(void)
{
    if (!btn_pending) {
        return;
    }
    btn_pending = 0;

    GPIOC->BSRR = (uint32_t)GPIO_PIN_9 << 16U;

    uint32_t latency = perf_now() - btn_t_event;
    perf_add(&st_btn_latency, latency);

    printf("EVT t_us=%lu name=btn_press\r\n", (unsigned long)perf_cycles_to_us(btn_t_event));
    printf("EVT t_us=%lu name=capture_start latency_us=%lu\r\n", 
           (unsigned long)perf_cycles_to_us(perf_now()), 
           (unsigned long)perf_cycles_to_us(latency));
}

void App_Poll(void)
{
    Console_Poll();
    button_task();
    temp_task();
    camera_task();
    led_task();
    tick_task();
    Process_Button_Event();
}
