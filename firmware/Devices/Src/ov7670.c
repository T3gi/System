/**
 * @file  ov7670.c
 * @brief OV7670 register configuration over SCCB (modes, window, auto loops).
 */
#include "ov7670.h"
#include "camera_blob.h"
#include "platform_sccb.h"
#include "platform_time.h"
#include <stdio.h>

static void ov7670_apply_regs(const uint8_t regs[][2], int nregs)
{
    for (int i = 0; i < nregs; i++) {
        uint8_t reg = regs[i][0];
        uint8_t val = regs[i][1];
        if (reg == 0xFF && val == 0xFF) break;      /* END marker */

        sccb_write_reg(0x21, reg, val);

        if (reg == 0x12 && val == 0x80) {
            /* Soft reset — the chip is unusable for ~1 ms after this. */
            Platform_DelayMs(100);
        }
    }
}

/**
 * @brief Program the sensor's hardware window.
 *
 * Straight port of Linux ov7670_set_hw().  The low bits of hstart/hstop live
 * in HREF[2:0] / HREF[5:3] and the low bits of vstart/vstop in VREF[1:0] /
 * VREF[3:2], so both need read-modify-write to preserve the upper bits (HREF
 * top two bits are an undocumented "edge offset" that must not be clobbered).
 */
void ov7670_set_hw(int hstart, int hstop, int vstart, int vstop)
{
    sccb_write_reg(0x21, REG_HSTART, (uint8_t)((hstart >> 3) & 0xFF));
    sccb_write_reg(0x21, REG_HSTOP,  (uint8_t)((hstop  >> 3) & 0xFF));
    int v = sccb_read_reg(0x21, REG_HREF);
    if (v < 0) v = 0x80;
    v = (v & 0xC0) | ((hstop & 0x07) << 3) | (hstart & 0x07);
    Platform_DelayMs(10);
    sccb_write_reg(0x21, REG_HREF, (uint8_t)v);

    sccb_write_reg(0x21, REG_VSTART, (uint8_t)((vstart >> 2) & 0xFF));
    sccb_write_reg(0x21, REG_VSTOP,  (uint8_t)((vstop  >> 2) & 0xFF));
    v = sccb_read_reg(0x21, REG_VREF);
    if (v < 0) v = 0x00;
    v = (v & 0xF0) | ((vstop & 0x03) << 2) | (vstart & 0x03);
    Platform_DelayMs(10);
    sccb_write_reg(0x21, REG_VREF, (uint8_t)v);
}

/* Currently selected mode index (see ov7670_modes[] in camera_blob.h). */
static int s_mode = 1;   /* default: CIF 352x288 full FOV */

/**
 * @brief Full sensor bring-up: common init, then the selected mode's
 *        register set, then the mode's hardware window.
 */
void ov7670_configure(int mode)
{
    if (mode < 0 || mode >= (int)OV7670_MODE_COUNT) mode = 0;
    s_mode = mode;
    const ov7670_mode_t *m = &ov7670_modes[mode];

    ov7670_apply_regs(ov7670_common_regs, (int)(sizeof(ov7670_common_regs)/2));
    ov7670_apply_regs(m->regs, m->nregs);
    ov7670_set_hw(m->hstart, m->hstop, m->vstart, m->vstop);

    printf("MODE %d %s nominal=%ux%u window=h[%u..%u] v[%u..%u]\r\n",
           mode, m->name, m->width, m->height,
           m->hstart, m->hstop, m->vstart, m->vstop);
}

/**
 * @brief Freeze / unfreeze the automatic exposure, gain and white balance.
 *
 * Essential for multi-pass strip capture: each strip comes from a different
 * physical frame, so if AEC/AGC/AWB keep adapting between passes the strips
 * end up with different brightness and the assembled image shows horizontal
 * banding.  Clearing the enable bits in COM8 latches whatever values the
 * loops had converged to, which keeps every subsequent frame identical.
 */
void ov7670_freeze_auto(int freeze)
{
    int com8 = sccb_read_reg(0x21, REG_COM8);
    if (com8 < 0) com8 = 0xE7;
    if (freeze) {
        /* Keep FASTAEC/AECSTEP/BFILT, drop AGC | AWB | AEC. */
        com8 &= (uint8_t)~(COM8_AGC | COM8_AWB | COM8_AEC);
    } else {
        com8 |= (COM8_AGC | COM8_AWB | COM8_AEC);
    }
    sccb_write_reg(0x21, REG_COM8, (uint8_t)com8);

    int gain  = sccb_read_reg(0x21, REG_GAIN);
    int aech  = sccb_read_reg(0x21, REG_AECH);
    int aechh = sccb_read_reg(0x21, REG_AECHH);
    int vref  = sccb_read_reg(0x21, REG_VREF);
    printf("AUTO %s COM8=0x%02X GAIN=0x%02X AECH=0x%02X AECHH=0x%02X VREF=0x%02X\r\n",
           freeze ? "frozen" : "enabled", com8 & 0xFF,
           gain & 0xFF, aech & 0xFF, aechh & 0xFF, vref & 0xFF);
}

int ov7670_current_mode(void) { return s_mode; }

unsigned ov7670_mode_count(void) { return (unsigned)OV7670_MODE_COUNT; }

const ov7670_mode_t *ov7670_mode_info(int mode)
{
    if (mode < 0 || mode >= (int)OV7670_MODE_COUNT) return 0;
    return &ov7670_modes[mode];
}
