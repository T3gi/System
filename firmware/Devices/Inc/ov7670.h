/**
 * @file  ov7670.h
 * @brief OV7670 sensor configuration over SCCB (platform-independent).
 */
#ifndef OV7670_H
#define OV7670_H

#include <stdint.h>

#define OV7670_SCCB_ADDR  0x21          /* 7-bit SCCB slave address */

/* ==========================================================================
 * Output modes.  Each entry is a register set applied after the common init
 * plus the hardware window HSTART/HSTOP/HREF and VSTART/VSTOP/VREF.
 *
 * Note hstop < hstart for several modes: the window wraps around the
 * 784-pixel total line length.  Width = (hstop - hstart) mod 784.
 * ========================================================================== */
typedef struct {
    const char    *name;
    const uint8_t (*regs)[2];
    uint16_t       nregs;      /* including END marker */
    uint16_t       width;      /* nominal output width  (pixels) */
    uint16_t       height;     /* nominal output height (lines)  */
    uint16_t       hstart, hstop, vstart, vstop;
} ov7670_mode_t;

/** Common init + mode register set + hardware window; prints a MODE line. */
void ov7670_configure(int mode);

/** Program HSTART/HSTOP/HREF and VSTART/VSTOP/VREF. */
void ov7670_set_hw(int hstart, int hstop, int vstart, int vstop);

/** 1 = latch AEC/AGC/AWB, 0 = let them run again; prints an AUTO line. */
void ov7670_freeze_auto(int freeze);

/** Mode last passed to ov7670_configure (CIF until then). */
int ov7670_current_mode(void);

unsigned ov7670_mode_count(void);

/** Mode table entry, or NULL if mode is out of range. */
const ov7670_mode_t *ov7670_mode_info(int mode);

#endif /* OV7670_H */
