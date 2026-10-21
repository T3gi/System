/**
 * @file  platform_camera.c
 * @brief OV7670 parallel bus (VSYNC/HREF/PCLK + D0..D7) and control pins.
 *
 * STM32F411 has no DCMI, so frames are read by polling GPIO.  The loops below
 * are timing-critical and were moved here unchanged from main.c.
 */
#include "platform_camera.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>

void Camera_HardReset(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
    HAL_Delay(50);
}

/** Set PWDN state via PA1.  0=LOW, 1=HIGH. */
void Camera_SetPwdn(int high)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_Delay(20);
}

/* ==================================================================
 * Frame capture (F401 has no DCMI — polling GPIO instead).
 *
 * Pin map:
 *   VSYNC -> PB10   (frame boundary)
 *   HREF  -> PB4    (line valid = HIGH during pixel bytes)
 *   PCLK  -> PB5    (rising edge = sample pixel byte)
 *   D0..D7 -> PC0..PC7   (parallel, read as GPIOC->IDR & 0xFF)
 *
 * Sensor is configured for QCIF (176x144) YUV422:
 *   176 * 144 * 2 = 50688 bytes/frame.
 * With PLLx4 and CLKRC=0x9F (/32 divider), effective PCLK ≈ 2.6 MHz,
 * so we have ~32 CPU cycles per pixel — enough for a tight polling loop.
 */
uint8_t cam_framebuffer[CAM_BUF_BYTES] __attribute__((aligned(4)));

#define CAM_VSYNC   GPIO_PIN_1
#define CAM_HREF    GPIO_PIN_4
#define CAM_PCLK    GPIO_PIN_0

/* Frame telemetry counters — filled by the capture routines. */
volatile uint32_t cam_last_lines_captured = 0;
volatile uint32_t cam_last_href_dropouts  = 0;
/* Shortest / longest line actually delivered in the last strip.  If these are
 * not both equal to the requested line length, the sensor geometry and our
 * read length disagree and the image will be distorted. */
volatile uint32_t cam_last_line_min = 0;
volatile uint32_t cam_last_line_max = 0;
/* Lines that did not deliver the full requested byte count. */
volatile uint32_t cam_last_short_lines = 0;

/**
 * @brief Block until the start of the next frame's pixel data.
 *
 * Sequence: wait for VSYNC low (we may be sitting inside a pulse), then for
 * the rising edge (start of the vertical sync pulse), then for the falling
 * edge — at which point the first HREF of the new frame is imminent.
 * Runs with interrupts enabled so HAL_GetTick() can drive the timeout.
 */
int cam_sync_frame_start(uint32_t timeout_ms)
{
    volatile uint32_t *A_IDR = &GPIOA->IDR;
    uint32_t t0 = HAL_GetTick();
    while ((*A_IDR & CAM_VSYNC) != 0)
        if (HAL_GetTick() - t0 > timeout_ms) return -1;
    while ((*A_IDR & CAM_VSYNC) == 0)
        if (HAL_GetTick() - t0 > timeout_ms) return -1;
    while ((*A_IDR & CAM_VSYNC) != 0)
        if (HAL_GetTick() - t0 > timeout_ms) return -1;
    return 0;
}

/* Poll-loop guard: worst case a line lasts ~1 ms == ~84000 cycles, so a
 * couple of million iterations is a generous ceiling that still bails out
 * instead of hanging the firmware if the camera stops clocking. */
#define CAM_GUARD  3000000u
/* Per-line guard for the sampling loop.  A VGA line is 1280 bytes at ~10
 * cycles per poll iteration, so ~200k iterations is far beyond normal while
 * still bounding the loop: COM10=0x20 suppresses PCLK during blanking, so an
 * unbounded wait for one more edge than the line contains would hang the
 * firmware outright (it did, silently, for two debugging sessions). */
#define CAM_LINE_GUARD  400000u

/**
 * @brief Read one line: wait for HREF, then sample every PCLK rising edge
 *        until HREF drops.
 *
 * Shared by the strip and snapshot paths so there is exactly one copy of the
 * timing-critical logic.  See cam_capture_strip() for why the loop is bounded
 * by HREF and why GPIOC is read in the same iteration as GPIOB.
 *
 * @return 0 on success, -1 if VSYNC arrived first (frame ended), -2 on stall.
 */
static int cam_read_line(uint8_t *dst, uint32_t line_bytes, uint32_t *out_cnt)
{
    volatile uint32_t *A_IDR = &GPIOA->IDR;
    volatile uint32_t *C_IDR = &GPIOC->IDR;
    uint32_t guard = CAM_LINE_GUARD;

    for (;;) {
        uint32_t sa = *A_IDR;
        if (sa & CAM_HREF)  break;
        if (sa & CAM_VSYNC) return -1;
        if (--guard == 0)   return -2;
    }

    uint32_t cnt = 0, prev = 0;
    guard = CAM_LINE_GUARD;
    for (;;) {
        uint32_t sa = *A_IDR;
        uint32_t sc = *C_IDR;
        if ((sa & CAM_HREF) == 0) break;
        uint32_t cur = sa & CAM_PCLK;
        if (cur != 0 && prev == 0 && cnt < line_bytes) {
            dst[cnt++] = (uint8_t)(sc & 0xFFu);
        }
        prev = cur;
        if (--guard == 0) return -2;
    }
    *out_cnt = cnt;
    while (cnt < line_bytes) dst[cnt++] = 0;   /* keep the row rectangular */
    return 0;
}

/**
 * @brief Single-frame snapshot: 2x2 box average of the current mode into
 *        SNAP_W x SNAP_H, taken from ONE frame.
 *
 * Unlike the strip path, nothing here is composited across frames, so a moving
 * subject cannot tear the result.  Requires the sensor to be in a mode whose
 * output is exactly twice the snapshot size (CIF 352x288).
 *
 * The two input lines are buffered raw and averaged in the horizontal blanking
 * gap that follows them, which keeps the arithmetic out of the sampling loop
 * where it would eat the timing margin.
 *
 * @return bytes produced, or a negative error code.
 */
int cam_snapshot(uint32_t in_w, uint32_t *out_short)
{
    if (in_w != SNAP_W * 2u) return -5;
    const uint32_t in_lb = in_w * 2u;                 /* 704 for CIF */
    static uint8_t raw[SNAP_W * 4u];
    static uint16_t acc[SNAP_W * 2u];      /* U, Y0, V, Y1 per output group */
    uint32_t shortlines = 0;

    if (cam_sync_frame_start(2000) != 0) return -1;

    uint8_t *out = cam_framebuffer;
    __disable_irq();
    const uint32_t groups = SNAP_W / 2u;
    for (uint32_t oy = 0; oy < SNAP_H; oy++) {
        /* UYVY in, UYVY out.  One output group (U Y0 V Y1) covers two input
         * groups on each of two lines, i.e. a 2x2 block of pixels:
         *   U  = mean of both U samples on both lines
         *   Y0 = mean of the two luma samples of the first input group
         *   Y1 = mean of the two luma samples of the second input group
         *   V  = mean of both V samples on both lines
         * Every captured pixel therefore contributes exactly once.
         *
         * The two lines are accumulated in separate blanking gaps so neither
         * gap has to carry the whole cost. */
        for (uint32_t half = 0; half < 2u; half++) {
            uint32_t cnt = 0;
            if (cam_read_line(raw, in_lb, &cnt) != 0) { __enable_irq(); return -6; }
            if (cnt != in_lb) shortlines++;

            const uint8_t *a = raw;
            uint16_t *ac = acc;
            if (half == 0) {
                for (uint32_t ox = 0; ox < groups; ox++) {
                    ac[0] = (uint16_t)(a[0] + a[4]);
                    ac[1] = (uint16_t)(a[1] + a[3]);
                    ac[2] = (uint16_t)(a[2] + a[6]);
                    ac[3] = (uint16_t)(a[5] + a[7]);
                    ac += 4; a += 8;
                }
            } else {
                for (uint32_t ox = 0; ox < groups; ox++) {
                    out[0] = (uint8_t)((ac[0] + a[0] + a[4]) >> 2);
                    out[1] = (uint8_t)((ac[1] + a[1] + a[3]) >> 2);
                    out[2] = (uint8_t)((ac[2] + a[2] + a[6]) >> 2);
                    out[3] = (uint8_t)((ac[3] + a[5] + a[7]) >> 2);
                    out += 4; ac += 4; a += 8;
                }
            }
        }
    }
    __enable_irq();
    if (out_short) *out_short = shortlines;
    return (int)(out - cam_framebuffer);
}

/**
 * @brief Capture `n_lines` lines starting at line `start_line` of the frame,
 *        reading exactly `line_bytes` bytes from each.
 *
 * Lines before the window are skipped by counting HREF pulses only, which is
 * cheap, so the strip boundaries land on exactly the same sensor rows every
 * pass.  That is what makes multi-pass assembly seamless.
 *
 * @return number of bytes stored, or a negative error code.
 */
int cam_capture_strip(uint32_t line_bytes, uint32_t start_line, uint32_t n_lines)
{
    if (line_bytes > MAX_LINE_BYTES) return -2;
    if (n_lines * line_bytes > STRIP_BYTES) return -3;

    volatile uint32_t *A_IDR = &GPIOA->IDR;
    volatile uint32_t *C_IDR = &GPIOC->IDR;
    uint8_t *p = cam_framebuffer;
    uint32_t lines = 0, dropouts = 0;
    uint32_t cnt_min = 0xFFFFFFFFu, cnt_max = 0, shortlines = 0;
    int stall = 0;

    if (cam_sync_frame_start(2000) != 0) return -1;

    __disable_irq();

    /* --- Skip to the top of the requested strip -------------------------- */
    for (uint32_t i = 0; i < start_line; i++) {
        uint32_t guard = CAM_GUARD;
        while ((*A_IDR & CAM_HREF) == 0) {
            if ((*A_IDR & CAM_VSYNC) != 0) { dropouts = n_lines; goto done; }
            if (--guard == 0) { dropouts = n_lines; goto done; }
        }
        guard = CAM_GUARD;
        while ((*A_IDR & CAM_HREF) != 0) { if (--guard == 0) goto done; }
    }

    /* --- Read the strip -------------------------------------------------- */
    for (uint32_t line = 0; line < n_lines; line++) {
        uint32_t guard = CAM_GUARD;
        while ((*A_IDR & CAM_HREF) == 0) {
            if ((*A_IDR & CAM_VSYNC) != 0) { dropouts = n_lines - line; goto done; }
            if (--guard == 0) { dropouts = n_lines - line; goto done; }
        }
        lines++;

        /* Sample every PCLK rising edge for as long as HREF stays asserted.
         *
         * Bounding the loop by HREF rather than by a fixed byte count is what
         * keeps each line self-aligning.  The previous version read exactly
         * line_bytes and tried to fix the start phase by first waiting for
         * PCLK to go low — but that discards byte 0 whenever PCLK happens to
         * be high when HREF rises, so the fixed-count read ran one byte past
         * the line end and stole a byte from the next line.  The error
         * accumulated down the strip and showed up as a progressive
         * horizontal shear that reset at every strip boundary.
         *
         * prev starts at 0 deliberately: if PCLK is already high when we
         * notice HREF, that high phase *is* byte 0 (we detect HREF within a
         * few cycles, far less than a PCLK period), so it must be sampled.
         */
        uint32_t cnt = 0, prev = 0;
        guard = CAM_LINE_GUARD;
        for (;;) {
            uint32_t sa = *A_IDR;
            /* Sample the data bus in the same iteration as the clock, two
             * cycles after it, instead of only after deciding an edge
             * occurred.  At VGA rates PCLK's high phase is ~16 CPU cycles, so
             * the extra few cycles the old ordering spent before reading
             * GPIOC were enough to occasionally latch the *next* byte.  That
             * shifted single pixels by one byte, swapping U and V for that
             * pair, and showed up as a fine pink/green checkerboard. */
            uint32_t sc = *C_IDR;
            if ((sa & CAM_HREF) == 0) break;          /* end of line */
            uint32_t cur = sa & CAM_PCLK;
            if (cur != 0 && prev == 0 && cnt < line_bytes) {
                *p++ = (uint8_t)(sc & 0xFFu);
                cnt++;
            }
            prev = cur;
            if (--guard == 0) { stall = 1; goto done; }
        }
        /* Track the real sample count before padding.  Padding used to hide a
         * missed PCLK edge behind a perfect-looking 1280, which cost a long
         * debugging detour: the line was one byte short, so U/V rotated and
         * the row came out magenta. */
        if (cnt_min > cnt) cnt_min = cnt;
        if (cnt_max < cnt) cnt_max = cnt;
        if (cnt != line_bytes) shortlines++;

        /* Keep the strip rectangular so a glitch shifts nothing below it. */
        while (cnt < line_bytes) { *p++ = 0; cnt++; }
        guard = CAM_GUARD;
        while ((*A_IDR & CAM_HREF) != 0) { if (--guard == 0) goto done; }
    }
done:
    __enable_irq();
    cam_last_lines_captured = lines;
    cam_last_href_dropouts  = dropouts;
    cam_last_line_min = (cnt_min == 0xFFFFFFFFu) ? 0 : cnt_min;
    cam_last_line_max = cnt_max;
    cam_last_short_lines = shortlines;
    if (stall) return -4;
    return (int)(p - cam_framebuffer);
}

/**
 * @brief Measure what the sensor is actually emitting, independent of any
 *        assumption about the configured mode.
 *
 * Counts HREF pulses between two VSYNC pulses and, for each line, counts PCLK
 * rising edges.  The edge detector samples GPIOA in a ~7-cycle loop while
 * PCLK's period is ~32 cycles at CLKRC=/32, so no edges are missed.
 *
 * This is the ground truth used to pick strip geometry — it caught the
 * 72-unique-lines behaviour of built-in QCIF that telemetry had hidden.
 */
void cam_probe(void)
{
    volatile uint32_t *A_IDR = &GPIOA->IDR;
    uint32_t lines = 0;
    uint32_t first = 0, mn = 0xFFFFFFFFu, mx = 0;
    uint32_t t_start, t_end;

    if (cam_sync_frame_start(2000) != 0) { printf("ERR probe: no VSYNC\r\n"); return; }

    t_start = HAL_GetTick();
    __disable_irq();
    for (;;) {
        /* Wait for the next HREF, or bail out when the frame ends. */
        uint32_t guard = CAM_GUARD;
        for (;;) {
            uint32_t s = *A_IDR;
            if (s & CAM_HREF)  break;
            if (s & CAM_VSYNC) goto frame_done;
            if (--guard == 0)  goto frame_done;
        }
        /* Count PCLK rising edges for as long as HREF stays asserted. */
        uint32_t cnt = 0, prev = 0;
        for (;;) {
            uint32_t s = *A_IDR;
            if ((s & CAM_HREF) == 0) break;
            uint32_t cur = s & CAM_PCLK;
            if (cur && !prev) cnt++;
            prev = cur;
        }
        if (lines == 0) first = cnt;
        if (cnt < mn) mn = cnt;
        if (cnt > mx) mx = cnt;
        lines++;
        if (lines > 2000) break;   /* safety */
    }
frame_done:
    __enable_irq();
    t_end = HAL_GetTick();

    if (mn == 0xFFFFFFFFu) mn = 0;
    printf("PROBE href_lines=%lu bytes_first=%lu bytes_min=%lu bytes_max=%lu frame_ms=%lu\r\n",
           (unsigned long)lines, (unsigned long)first,
           (unsigned long)mn, (unsigned long)mx,
           (unsigned long)(t_end - t_start));
    printf("PROBE implies width=%lu px height=%lu lines\r\n",
           (unsigned long)(first / 2u), (unsigned long)lines);
}

/**
 * @brief Compare two captured lines byte-for-byte.  Used to detect the
 *        sensor's line-doubling behaviour automatically.
 */
uint32_t cam_line_diff(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint32_t d = 0;
    for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) d++;
    return d;
}
