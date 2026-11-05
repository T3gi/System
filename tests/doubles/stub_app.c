#include "stub_app.h"
#include "bus_log.h"
#include "fake_time.h"
#include "perf.h"
#include "console.h"
#include "journal.h"
#include "mpu6050.h"
#include "ov7670.h"
#include "platform_board.h"
#include "selftest.h"
#include "servo.h"
#include <string.h>

#define ALL_LINES  0xFFFFFFFFu

bool  stub_camera_ok, stub_flash_ok, stub_journal_init_ok, stub_journal_append_ok;
bool  stub_temp_ok;
float stub_temp;
bool  stub_button, stub_led;

uint8_t  stub_cam_luma;
int      stub_cam_ret;
ov7670_capture_stats_t stub_cam_stats;
uint32_t stub_cam_ms;
uint16_t stub_cam_width;

fake_gpio_t fake_gpioc;
uint32_t    fake_cycles;

static uint8_t s_frame[CAM_BUF_BYTES];      /* same size as the real buffer */
static ov7670_capture_stats_t s_last;

void stub_app_reset(void)
{
    stub_camera_ok = stub_flash_ok = stub_journal_init_ok = stub_journal_append_ok = true;
    stub_temp_ok = true;
    stub_temp = 25.0f;
    stub_button = false;
    stub_led = false;
    stub_cam_luma = 50;         /* between the dark and the light level */
    stub_cam_ret = 0;
    memset(&stub_cam_stats, 0, sizeof stub_cam_stats);
    stub_cam_stats.lines = ALL_LINES;
    stub_cam_ms = 0;
    stub_cam_width = 352;
    fake_gpioc.BSRR = 0;
    fake_cycles = 0;
}

/* --- App -------------------------------------------------------------- */

selftest_result_t SelfTest_Run(void)
{
    selftest_result_t r = {0};
    bus_log("SELFTEST;");
    r.w25q64 = stub_flash_ok;
    r.ov7670 = stub_camera_ok;
    return r;
}

void     Console_Help(void)         {}
void     Console_Poll(void)         { bus_log("CONSOLE;"); }
void     Console_CaptureFrame(void) { bus_log("BUTTON FRAME;"); }
uint32_t Console_CommandCount(void) { return 3; }

bool Journal_Init(void)             { bus_log("JOURNAL INIT;"); return stub_journal_init_ok; }
void Journal_PrintLast(unsigned n)  { bus_log("JOURNAL PRINT %u;", n); }

bool Journal_Append(uint8_t state, uint8_t source, int16_t temp_c100, uint32_t t_ms)
{
    bus_log("JOURNAL %u %u %d %u;", (unsigned)state, (unsigned)source,
            (int)temp_c100, (unsigned)t_ms);
    return stub_journal_append_ok;
}

/* --- Devices ------------------------------------------------------------ */

void Servo_SetAngle(uint8_t angle)  { bus_log("SERVO %u;", (unsigned)angle); }

bool MPU6050_ReadTemperature(float *temp_c)
{
    bus_log("MPU;");
    if (stub_temp_ok) *temp_c = stub_temp;
    return stub_temp_ok;
}

void ov7670_configure(int mode)     { bus_log("CAM CONFIG %d;", mode); }
int  ov7670_current_mode(void)      { return 1; }

const ov7670_mode_t *ov7670_mode_info(int mode)
{
    static ov7670_mode_t m;
    (void)mode;
    m.name = "CIF";
    m.width = stub_cam_width;
    return &m;
}

/* A UYVY picture of uniform brightness; U and V are bright on purpose, so a
 * mean taken over the wrong bytes would be noticed. */
int OV7670_CaptureStrip(uint32_t line_bytes, uint32_t start, uint32_t n)
{
    bus_log("CAM STRIP %u %u %u;", (unsigned)line_bytes, (unsigned)start, (unsigned)n);
    fake_time_freeze(fake_time_now() + stub_cam_ms);

    s_last = stub_cam_stats;
    if (s_last.lines == ALL_LINES) s_last.lines = n;
    if (stub_cam_ret < 0) return stub_cam_ret;

    uint32_t bytes = line_bytes * n;
    for (uint32_t i = 0; i < bytes; i++) {
        s_frame[i] = (i & 1u) ? stub_cam_luma : 200u;
    }
    return (int)bytes;
}

uint8_t *OV7670_Buffer(void)                  { return s_frame; }
ov7670_capture_stats_t OV7670_LastCapture(void) { return s_last; }

/* --- Platform ------------------------------------------------------------ */

void     Platform_Console_Start(void)       { bus_log("CONSOLE START;"); }
uint32_t Platform_Console_RxBytes(void)     { return 7; }
uint32_t Platform_Console_RxOverruns(void)  { return 1; }
uint32_t Platform_Console_RxDropped(void)   { return 2; }
bool     Platform_ButtonPressed(void)       { return stub_button; }
void     Platform_LedSet(bool on)           { stub_led = on; }
