/**
 * @file  ov7670_capture.h
 * @brief OV7670 on this board: power-up, presence check, frame capture through
 *        the parallel port and SCCB bus diagnostics.
 *
 * App talks to the camera only through these functions and ov7670.h; the
 * Platform calls (platform_camera, platform_sccb) stay inside the driver.
 * platform_camera.h is included for the capture buffer geometry only
 * (STRIP_LINES, MAX_LINE_BYTES, SNAP_W/H, CAM_BUF_BYTES): its size is a
 * board decision (SRAM budget).
 */
#ifndef OV7670_CAPTURE_H
#define OV7670_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>
#include "platform_camera.h"

/* Telemetry of the last capture call. */
typedef struct {
    uint32_t lines;        /* lines captured                           */
    uint32_t dropouts;     /* HREF dropouts                            */
    uint32_t line_min;     /* shortest / longest line actually read    */
    uint32_t line_max;
    uint32_t short_lines;  /* lines shorter than requested             */
} ov7670_capture_stats_t;

/** XCLK, hardware reset, PWDN low, SCCB on the HAL back-end.  Prints progress.
 *  The SCCB pins are shared with I2C1, so call it after the I2C sensors. */
void OV7670_PowerUp(void);

/** Read PID/VER up to 5 times; true once they are 0x76/0x73. */
bool OV7670_Probe(void);

/** Register access: value 0..255 / 0 on success, negative on bus error. */
int  OV7670_ReadReg(uint8_t reg);
int  OV7670_WriteReg(uint8_t reg, uint8_t val);

/** Wait for the next frame start.  0 = OK, -1 = timeout. */
int  OV7670_SyncFrame(uint32_t timeout_ms);
/** Capture n lines from start into OV7670_Buffer().  Bytes or <0. */
int  OV7670_CaptureStrip(uint32_t line_bytes, uint32_t start, uint32_t n);
/** One frame, 2x2 averaged into SNAP_W x SNAP_H.  Bytes or <0. */
int  OV7670_Snapshot(uint32_t in_w, uint32_t *short_lines);
/** Measure and print the real HREF/PCLK geometry of one frame. */
void OV7670_ProbeGeometry(void);
/** Count differing bytes between two lines. */
uint32_t OV7670_LineDiff(const uint8_t *a, const uint8_t *b, uint32_t n);

/** Capture buffer (CAM_BUF_BYTES), also usable as scratch between captures. */
uint8_t *OV7670_Buffer(void);
ov7670_capture_stats_t OV7670_LastCapture(void);

/* --- SCCB bus diagnostics (UART commands SCCB / SCCBTEST) --- */
/** Select back-end 0=HAL 1=HAL_MEM 2=BITBANG with n attempts.
 *  0 = OK, -2 = no such back-end, other <0 = init failed. */
int  OV7670_BusSelect(int backend, int attempts);
/** Print one "SCCB ..." line with the current back-end and its counters. */
void OV7670_BusReport(void);
/** Reliability test of one back-end (or all with which = -1). */
void OV7670_BusTest(int which, int n, int attempts, int stop_wait);

#endif /* OV7670_CAPTURE_H */
