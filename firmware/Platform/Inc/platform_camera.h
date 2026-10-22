/**
 * @file  platform_camera.h
 * @brief OV7670 parallel bus capture and control pins on this board.
 *
 *   VSYNC PA1, HREF PA4, PCLK PA0, D0..D7 PC0..PC7, RESET PB1, PWDN PB2.
 */
#ifndef PLATFORM_CAMERA_H
#define PLATFORM_CAMERA_H

#include <stdint.h>

/* Largest line we ever read: the full 656-pixel array = 1312 bytes YUV422.
 * (Plain VGA needs only 1280; the extra covers the wider full-array window.) */
#define MAX_LINE_BYTES   1312u
/* Strip height.  40 KB fits comfortably in the 96 KB SRAM alongside the HAL
 * and the stack, and divides both 480 (VGA, 15 passes) and 288 (CIF, 9). */
#define STRIP_LINES      32u
#define STRIP_BYTES      (MAX_LINE_BYTES * STRIP_LINES)

/* Single-frame snapshot output: CIF 352x288 is exactly 2x(176x144), so a 2x2
 * box average taken while the frame streams needs only the *output* in RAM.
 * That makes a genuine one-instant capture possible at the target resolution
 * despite there being no FIFO and no room for a full CIF frame. */
#define SNAP_W           176u
#define SNAP_H           144u
#define SNAP_BYTES       (SNAP_W * SNAP_H * 2u)          /* 50688 */

/* One buffer serves both paths; only one is ever active. */
#define CAM_BUF_BYTES    ((STRIP_BYTES > SNAP_BYTES) ? STRIP_BYTES : SNAP_BYTES)
extern uint8_t cam_framebuffer[CAM_BUF_BYTES];

/* Frame telemetry counters — filled by the capture routines. */
extern volatile uint32_t cam_last_lines_captured;
extern volatile uint32_t cam_last_href_dropouts;
extern volatile uint32_t cam_last_line_min;
extern volatile uint32_t cam_last_line_max;
extern volatile uint32_t cam_last_short_lines;

/** Start XCLK on PA8 (TIM1_CH1, PCLK2 / 4, 50 % duty).  Returns the frequency
 *  in Hz.  The sensor does not answer on SCCB without it. */
uint32_t Camera_XclkStart(void);

/** Pulse the sensor RESET pin (PB1). */
void Camera_HardReset(void);
/** Set PWDN (PB2).  0 = LOW (running), 1 = HIGH (power down). */
void Camera_SetPwdn(int high);

/** Block until the next frame's pixel data starts.  0 = OK, -1 = timeout. */
int cam_sync_frame_start(uint32_t timeout_ms);

/** One frame, 2x2 averaged into SNAP_W x SNAP_H.  Bytes produced or <0. */
int cam_snapshot(uint32_t in_w, uint32_t *out_short);

/** Capture n_lines lines from start_line into cam_framebuffer.  Bytes or <0. */
int cam_capture_strip(uint32_t line_bytes, uint32_t start_line, uint32_t n_lines);

/** Measure and print the real HREF/PCLK geometry of one frame. */
void cam_probe(void);

/** Count differing bytes between two lines. */
uint32_t cam_line_diff(const uint8_t *a, const uint8_t *b, uint32_t n);

#endif /* PLATFORM_CAMERA_H */
