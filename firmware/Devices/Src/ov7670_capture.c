/**
 * @file  ov7670_capture.c
 * @brief OV7670 power-up, presence check, capture and SCCB diagnostics.
 *        Moved from main.c; see ov7670_capture.h.
 */
#include "ov7670_capture.h"
#include "ov7670.h"
#include "camera_blob.h"    /* REG_BRIGHT for the bus test */
#include "platform_sccb.h"
#include "platform_time.h"
#include <stdio.h>

#define OV7670_PID  0x76
#define OV7670_VER  0x73

void OV7670_PowerUp(void)
{
    sccb_pins_reinit();

    uint32_t xclk = Camera_XclkStart();
    printf(">> XCLK = %lu Hz on PA8 (TIM1_CH1, 50%% duty)\r\n", (unsigned long)xclk);
    Platform_DelayMs(100);

    Camera_HardReset();
    Camera_SetPwdn(0);

    /* SCCB through stock HAL I2C (see platform_sccb.h). */
    sccb_init(SCCB_HAL);
    printf(">> SCCB backend = %s\r\n", sccb_backend_name(sccb_backend()));
    Platform_DelayMs(50);
}

bool OV7670_Probe(void)
{
    for (int rep = 0; rep < 5; rep++) {
        int h = sccb_read_reg(OV7670_SCCB_ADDR, 0x0A);
        int l = sccb_read_reg(OV7670_SCCB_ADDR, 0x0B);
        bool ok = (h == OV7670_PID && l == OV7670_VER);
        printf(">> SCCB probe #%d: PID=%d(0x%02X) VER=%d(0x%02X) halerr=0x%lX%s\r\n",
               rep, h, h & 0xFF, l, l & 0xFF,
               (unsigned long)sccb_stats()->last_err, ok ? "  [OV7670 OK]" : "");
        Platform_DelayMs(50);
        if (ok) return true;
    }
    return false;
}

int OV7670_ReadReg(uint8_t reg)              { return sccb_read_reg(OV7670_SCCB_ADDR, reg); }
int OV7670_WriteReg(uint8_t reg, uint8_t val) { return sccb_write_reg(OV7670_SCCB_ADDR, reg, val); }

int OV7670_SyncFrame(uint32_t timeout_ms) { return cam_sync_frame_start(timeout_ms); }

int OV7670_CaptureStrip(uint32_t line_bytes, uint32_t start, uint32_t n)
{
    return cam_capture_strip(line_bytes, start, n);
}

int OV7670_Snapshot(uint32_t in_w, uint32_t *short_lines) { return cam_snapshot(in_w, short_lines); }

void OV7670_ProbeGeometry(void) { cam_probe(); }

uint32_t OV7670_LineDiff(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    return cam_line_diff(a, b, n);
}

uint8_t *OV7670_Buffer(void) { return cam_framebuffer; }

ov7670_capture_stats_t OV7670_LastCapture(void)
{
    ov7670_capture_stats_t s = {
        cam_last_lines_captured, cam_last_href_dropouts,
        cam_last_line_min, cam_last_line_max, cam_last_short_lines
    };
    return s;
}

/* ======================================================================= */
/* SCCB bus diagnostics                                                     */
/* ======================================================================= */
int OV7670_BusSelect(int backend, int attempts)
{
    if (backend < 0 || backend >= SCCB_BACKEND_COUNT) return -2;
    sccb_set_attempts(attempts);
    return sccb_init((sccb_backend_t)backend);
}

void OV7670_BusReport(void)
{
    const sccb_stats_t *st = sccb_stats();
    printf("SCCB %d %s xfers=%lu errors=%lu retries=%lu last_err=0x%lX\r\n",
           (int)sccb_backend(), sccb_backend_name(sccb_backend()),
           (unsigned long)st->xfers, (unsigned long)st->errors,
           (unsigned long)st->retries, (unsigned long)st->last_err);
}

/**
 * Exercise one back-end on the live sensor:
 *   - first read right after (re)init (the "warm-up" issue from lab2),
 *   - N reads of PID/VER (0x0A/0x0B) -> exact-match count + time per read,
 *   - 32 writes to BRIGHT (0x55) verified with the same back-end AND with the
 *     bit-bang reference (so a broken read path can't hide a working write),
 *   - HAL error code / bit-bang 9th-bit statistics.
 * The original BRIGHT value is restored afterwards.
 */
static void bus_test_backend(sccb_backend_t b, int n, int attempts)
{
    const uint8_t A = OV7670_SCCB_ADDR;
    sccb_init(SCCB_BITBANG);
    int bright0 = sccb_read_reg(A, REG_BRIGHT);

    sccb_set_attempts(attempts);
    sccb_init(b);
    sccb_stats_reset();

    int f_h = sccb_read_reg(A, 0x0A), f_l = sccb_read_reg(A, 0x0B);

    int ok = 0, bad = 0, err = 0;
    uint32_t cyc = 0;
    for (int i = 0; i < n; i++) {
        uint32_t t0 = Platform_CycleCount();
        int h = sccb_read_reg(A, 0x0A);
        int l = sccb_read_reg(A, 0x0B);
        cyc += Platform_CycleCount() - t0;
        if (h < 0 || l < 0) err++;
        else if (h == OV7670_PID && l == OV7670_VER) ok++;
        else bad++;
    }
    sccb_stats_t rd = *sccb_stats();

    int w_err = 0, w_self = 0, w_ref = 0;
    for (int i = 0; i < 32; i++) {
        uint8_t v = (uint8_t)(i * 8 + 3);
        sccb_init(b);
        if (sccb_write_reg(A, REG_BRIGHT, v) < 0) { w_err++; continue; }
        if (sccb_read_reg(A, REG_BRIGHT) == v) w_self++;
        sccb_init(SCCB_BITBANG);
        if (sccb_read_reg(A, REG_BRIGHT) == v) w_ref++;
    }

    printf("SCCBTEST %d %s\r\n", (int)b, sccb_backend_name(b));
    printf("  first-read  PID=%d VER=%d %s\r\n", f_h, f_l,
           (f_h == OV7670_PID && f_l == OV7670_VER) ? "OK" : "WRONG");
    printf("  reads       %d/%d OK  wrong=%d  error=%d  %lu us/read  retries=%lu last_err=0x%lX\r\n",
           ok, n, bad, err,
           (unsigned long)(cyc / (uint32_t)(2 * n) / Platform_CyclesPerUs()),
           (unsigned long)rd.retries, (unsigned long)rd.last_err);
    printf("  writes      32 sent  hal_err=%d  readback_self=%d/32  readback_bitbang=%d/32\r\n",
           w_err, w_self, w_ref);
    if (b == SCCB_BITBANG)
        printf("  9th bit     LOW(ack)=%lu HIGH(no-ack)=%lu\r\n",
               (unsigned long)rd.x_low, (unsigned long)rd.x_high);

    sccb_init(SCCB_BITBANG);
    if (bright0 >= 0) sccb_write_reg(A, REG_BRIGHT, (uint8_t)bright0);
}

void OV7670_BusTest(int which, int n, int attempts, int stop_wait)
{
    sccb_backend_t keep = sccb_backend();
    sccb_set_stop_wait(stop_wait);
    Platform_CycleCounterStart();
    for (int b = 0; b < SCCB_BACKEND_COUNT; b++)
        if (which < 0 || which == b) bus_test_backend((sccb_backend_t)b, n, attempts);
    sccb_set_attempts(3);
    sccb_init(keep);
    sccb_set_stop_wait(1);
    printf("SCCB backend restored: %s\r\n", sccb_backend_name(keep));
}
