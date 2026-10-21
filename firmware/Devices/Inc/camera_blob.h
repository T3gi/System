/*
 * camera_blob.h
 *
 * Magic Register Values from Linux OV7670 Driver
 * linux/drivers/media/i2c/ov7670.c
 * GPL-2.0
 *
 *
 *  Created on: May 28, 2021
 *      Author: pipat
 */

#ifndef INC_CWSYS_CAMERA_BLOB_H_
#define INC_CWSYS_CAMERA_BLOB_H_

#include <stdint.h>
#include "ov7670.h"

#define SCCB_ADDR 0x42

/* Registers */
#define REG_GAIN	0x00	/* Gain lower 8 bits (rest in vref) */
#define REG_BLUE	0x01	/* blue gain */
#define REG_RED		0x02	/* red gain */
#define REG_VREF	0x03	/* Pieces of GAIN, VSTART, VSTOP */
#define REG_COM1	0x04	/* Control 1 */
#define  COM1_CCIR656	  0x40  /* CCIR656 enable */
#define REG_BAVE	0x05	/* U/B Average level */
#define REG_GbAVE	0x06	/* Y/Gb Average level */
#define REG_AECHH	0x07	/* AEC MS 5 bits */
#define REG_RAVE	0x08	/* V/R Average level */
#define REG_COM2	0x09	/* Control 2 */
#define  COM2_SSLEEP	  0x10	/* Soft sleep mode */
#define REG_PID		0x0a	/* Product ID MSB */
#define REG_VER		0x0b	/* Product ID LSB */
#define REG_COM3	0x0c	/* Control 3 */
#define  COM3_SWAP	  0x40	  /* Byte swap */
#define  COM3_SCALEEN	  0x08	  /* Enable scaling */
#define  COM3_DCWEN	  0x04	  /* Enable downsamp/crop/window */
#define REG_COM4	0x0d	/* Control 4 */
#define REG_COM5	0x0e	/* All "reserved" */
#define REG_COM6	0x0f	/* Control 6 */
#define REG_AECH	0x10	/* More bits of AEC value */
#define REG_CLKRC	0x11	/* Clocl control */
#define   CLK_EXT	  0x40	  /* Use external clock directly */
#define   CLK_SCALE	  0x3f	  /* Mask for internal clock scale */
#define REG_COM7	0x12	/* Control 7 */
#define   COM7_RESET	  0x80	  /* Register reset */
#define   COM7_FMT_MASK	  0x38
#define   COM7_FMT_VGA	  0x00
#define	  COM7_FMT_CIF	  0x20	  /* CIF format */
#define   COM7_FMT_QVGA	  0x10	  /* QVGA format */
#define   COM7_FMT_QCIF	  0x08	  /* QCIF format */
#define	  COM7_RGB	  0x04	  /* bits 0 and 2 - RGB format */
#define	  COM7_YUV	  0x00	  /* YUV */
#define	  COM7_BAYER	  0x01	  /* Bayer format */
#define	  COM7_PBAYER	  0x05	  /* "Processed bayer" */
#define REG_COM8	0x13	/* Control 8 */
#define   COM8_FASTAEC	  0x80	  /* Enable fast AGC/AEC */
#define   COM8_AECSTEP	  0x40	  /* Unlimited AEC step size */
#define   COM8_BFILT	  0x20	  /* Band filter enable */
#define   COM8_AGC	  0x04	  /* Auto gain enable */
#define   COM8_AWB	  0x02	  /* White balance enable */
#define   COM8_AEC	  0x01	  /* Auto exposure enable */
#define REG_COM9	0x14	/* Control 9  - gain ceiling */
#define REG_COM10	0x15	/* Control 10 */
#define   COM10_HSYNC	  0x40	  /* HSYNC instead of HREF */
#define   COM10_PCLK_HB	  0x20	  /* Suppress PCLK on horiz blank */
#define   COM10_HREF_REV  0x08	  /* Reverse HREF */
#define   COM10_VS_LEAD	  0x04	  /* VSYNC on clock leading edge */
#define   COM10_VS_NEG	  0x02	  /* VSYNC negative */
#define   COM10_HS_NEG	  0x01	  /* HSYNC negative */
#define REG_HSTART	0x17	/* Horiz start high bits */
#define REG_HSTOP	0x18	/* Horiz stop high bits */
#define REG_VSTART	0x19	/* Vert start high bits */
#define REG_VSTOP	0x1a	/* Vert stop high bits */
#define REG_PSHFT	0x1b	/* Pixel delay after HREF */
#define REG_MIDH	0x1c	/* Manuf. ID high */
#define REG_MIDL	0x1d	/* Manuf. ID low */
#define REG_MVFP	0x1e	/* Mirror / vflip */
#define   MVFP_MIRROR	  0x20	  /* Mirror image */
#define   MVFP_FLIP	  0x10	  /* Vertical flip */

#define REG_AEW		0x24	/* AGC upper limit */
#define REG_AEB		0x25	/* AGC lower limit */
#define REG_VPT		0x26	/* AGC/AEC fast mode op region */
#define REG_HSYST	0x30	/* HSYNC rising edge delay */
#define REG_HSYEN	0x31	/* HSYNC falling edge delay */
#define REG_HREF	0x32	/* HREF pieces */
#define REG_TSLB	0x3a	/* lots of stuff */
#define   TSLB_YLAST	  0x04	  /* UYVY or VYUY - see com13 */
#define REG_COM11	0x3b	/* Control 11 */
#define   COM11_NIGHT	  0x80	  /* NIght mode enable */
#define   COM11_NMFR	  0x60	  /* Two bit NM frame rate */
#define   COM11_HZAUTO	  0x10	  /* Auto detect 50/60 Hz */
#define	  COM11_50HZ	  0x08	  /* Manual 50Hz select */
#define   COM11_EXP	  0x02
#define REG_COM12	0x3c	/* Control 12 */
#define   COM12_HREF	  0x80	  /* HREF always */
#define REG_COM13	0x3d	/* Control 13 */
#define   COM13_GAMMA	  0x80	  /* Gamma enable */
#define	  COM13_UVSAT	  0x40	  /* UV saturation auto adjustment */
#define   COM13_UVSWAP	  0x01	  /* V before U - w/TSLB */
#define REG_COM14	0x3e	/* Control 14 */
#define   COM14_DCWEN	  0x10	  /* DCW/PCLK-scale enable */
#define REG_EDGE	0x3f	/* Edge enhancement factor */
#define REG_COM15	0x40	/* Control 15 */
#define   COM15_R10F0	  0x00	  /* Data range 10 to F0 */
#define	  COM15_R01FE	  0x80	  /*            01 to FE */
#define   COM15_R00FF	  0xc0	  /*            00 to FF */
#define   COM15_RGB565	  0x10	  /* RGB565 output */
#define   COM15_RGB555	  0x30	  /* RGB555 output */
#define REG_COM16	0x41	/* Control 16 */
#define   COM16_AWBGAIN   0x08	  /* AWB gain enable */
#define REG_COM17	0x42	/* Control 17 */
#define   COM17_AECWIN	  0xc0	  /* AEC window - must match COM4 */
#define   COM17_CBAR	  0x08	  /* DSP Color bar */

/*
 * This matrix defines how the colors are generated, must be
 * tweaked to adjust hue and saturation.
 *
 * Order: v-red, v-green, v-blue, u-red, u-green, u-blue
 *
 * They are nine-bit signed quantities, with the sign bit
 * stored in 0x58.  Sign for v-red is bit 0, and up from there.
 */
#define	REG_CMATRIX_BASE 0x4f
#define   CMATRIX_LEN 6
#define REG_CMATRIX_SIGN 0x58


#define REG_BRIGHT	0x55	/* Brightness */
#define REG_CONTRAS	0x56	/* Contrast control */

#define REG_GFIX	0x69	/* Fix gain control */

#define REG_DBLV	0x6b	/* PLL control an debugging */
#define   DBLV_BYPASS	  0x0a	  /* Bypass PLL */
#define   DBLV_X4	  0x4a	  /* clock x4 */
#define   DBLV_X6	  0x8a	  /* clock x6 */
#define   DBLV_X8	  0xca	  /* clock x8 */

#define REG_SCALING_XSC	0x70	/* Test pattern and horizontal scale factor */
#define   TEST_PATTTERN_0 0x80
#define REG_SCALING_YSC	0x71	/* Test pattern and vertical scale factor */
#define   TEST_PATTTERN_1 0x80

#define REG_REG76	0x76	/* OV's name */
#define   R76_BLKPCOR	  0x80	  /* Black pixel correction enable */
#define   R76_WHTPCOR	  0x40	  /* White pixel correction enable */

#define REG_RGB444	0x8c	/* RGB 444 control */
#define   R444_ENABLE	  0x02	  /* Turn on RGB444, overrides 5x5 */
#define   R444_RGBX	  0x01	  /* Empty nibble at end */

#define REG_HAECC1	0x9f	/* Hist AEC/AGC control 1 */
#define REG_HAECC2	0xa0	/* Hist AEC/AGC control 2 */

#define REG_BD50MAX	0xa5	/* 50hz banding step limit */
#define REG_HAECC3	0xa6	/* Hist AEC/AGC control 3 */
#define REG_HAECC4	0xa7	/* Hist AEC/AGC control 4 */
#define REG_HAECC5	0xa8	/* Hist AEC/AGC control 5 */
#define REG_HAECC6	0xa9	/* Hist AEC/AGC control 6 */
#define REG_HAECC7	0xaa	/* Hist AEC/AGC control 7 */
#define REG_BD60MAX	0xab	/* 60hz banding step limit */

/* ==========================================================================
 * COMMON INIT — everything that is mode-independent.
 *
 * Derived from Linux ov7670_default_regs[] + ov7670_fmt_yuv422[].
 * The mode-specific tail (COM7 format bits, window, DCW/scaling) is applied
 * afterwards from one of the ov7670_mode_* arrays below, so that switching
 * resolution at runtime never leaves stale scaler state behind.
 * ========================================================================== */
static const uint8_t ov7670_common_regs[][2] ={
		{0x12, 0x80}, // Register Reset, Must wait 1ms
		{ 0x6B, 0x7A }, // PLL x4


		//================BEGIN Magic=================
		/*
		 * Set the hardware window.  These values from OV don't entirely
		 * make sense - hstop is less than hstart.  But they work...
		 */

		{ REG_HSTART, 0x13 }, { REG_HSTOP, 0x01 }, { REG_HREF, 0xb6 }, {
				REG_VSTART, 0x02 }, { REG_VSTOP, 0x7a }, { REG_VREF, 0x0a },

		{ REG_COM3, 0 }, { REG_COM14, 0 },

		/* Mystery scaling numbers */

		{ REG_SCALING_XSC, 0x3a }, { REG_SCALING_YSC, 0x35 }, { 0x72, 0x11 }, {
				0x73, 0xf0 }, { 0xa2, 0x02 }, { REG_COM10, 0x0 },

		/* Gamma curve values */
		{ 0x7a, 0x20 }, { 0x7b, 0x10 }, { 0x7c, 0x1e }, { 0x7d, 0x35 }, { 0x7e,
				0x5a }, { 0x7f, 0x69 }, { 0x80, 0x76 }, { 0x81, 0x80 }, { 0x82,
				0x88 }, { 0x83, 0x8f }, { 0x84, 0x96 }, { 0x85, 0xa3 }, { 0x86,
				0xaf }, { 0x87, 0xc4 }, { 0x88, 0xd7 }, { 0x89, 0xe8 },

		/* AGC and AEC parameters.  Note we start by disabling those features,
		 then turn them only after tweaking the values. */
		{ REG_COM8, COM8_FASTAEC | COM8_AECSTEP | COM8_BFILT }, { REG_GAIN, 0 },
		{ REG_AECH, 0 }, { REG_COM4, 0x40 }, /* magic reserved bit */
		{ REG_COM9, 0x18 }, /* 4x gain + magic rsvd bit */
		{ REG_BD50MAX, 0x05 }, { REG_BD60MAX, 0x07 }, { REG_AEW, 0x95 }, {
				REG_AEB, 0x33 }, { REG_VPT, 0xe3 }, { REG_HAECC1, 0x78 }, {
				REG_HAECC2, 0x68 }, { 0xa1, 0x03 }, /* magic */
		{ REG_HAECC3, 0xd8 }, { REG_HAECC4, 0xd8 }, { REG_HAECC5, 0xf0 }, {
				REG_HAECC6, 0x90 }, { REG_HAECC7, 0x94 },
		{ REG_COM8, COM8_FASTAEC | COM8_AECSTEP | COM8_BFILT | COM8_AGC
				| COM8_AEC },

		/* Almost all of these are magic "reserved" values.  */
		{ REG_COM5, 0x61 }, { REG_COM6, 0x4b }, { 0x16, 0x02 },
		{ REG_MVFP, 0x07 }, { 0x21, 0x02 }, { 0x22, 0x91 }, { 0x29, 0x07 }, {
				0x33, 0x0b }, { 0x35, 0x0b }, { 0x37, 0x1d }, { 0x38, 0x71 }, {
				0x39, 0x2a }, { REG_COM12, 0x78 }, { 0x4d, 0x40 },
		{ 0x4e, 0x20 }, { REG_GFIX, 0 }, { 0x6b, 0x4a }, { 0x74, 0x10 }, { 0x8d,
				0x4f }, { 0x8e, 0 }, { 0x8f, 0 }, { 0x90, 0 }, { 0x91, 0 }, {
				0x96, 0 }, { 0x9a, 0 }, { 0xb0, 0x84 }, { 0xb1, 0x0c }, { 0xb2,
				0x0e }, { 0xb3, 0x82 }, { 0xb8, 0x0a },

		/* More reserved magic, some of which tweaks white balance */
		{ 0x43, 0x0a }, { 0x44, 0xf0 }, { 0x45, 0x34 }, { 0x46, 0x58 }, { 0x47,
				0x28 }, { 0x48, 0x3a }, { 0x59, 0x88 }, { 0x5a, 0x88 }, { 0x5b,
				0x44 }, { 0x5c, 0x67 }, { 0x5d, 0x49 }, { 0x5e, 0x0e }, { 0x6c,
				0x0a }, { 0x6d, 0x55 }, { 0x6e, 0x11 }, { 0x6f, 0x9f }, /* "9e for advance AWB" */
		{ 0x6a, 0x40 }, { REG_BLUE, 0x40 }, { REG_RED, 0x60 }, { REG_COM8,
				COM8_FASTAEC | COM8_AECSTEP | COM8_BFILT | COM8_AGC | COM8_AEC
						| COM8_AWB },

		/* Matrix coefficients */
		{ 0x4f, 0x80 }, { 0x50, 0x80 }, { 0x51, 0 }, { 0x52, 0x22 }, { 0x53,
				0x5e }, { 0x54, 0x80 }, { 0x58, 0x9e },

		{ REG_COM16, COM16_AWBGAIN }, { REG_EDGE, 0 }, { 0x75, 0x05 }, { 0x76,
				0xe1 }, { 0x4c, 0 }, { 0x77, 0x01 },

		//================END Magic=================

		{ 0x11, 0x9F }, // CLKRC — internal clock prescaler /32 (with PLLx4 => PCLK ~2.6 MHz)
		{ 0x6B, 0x7A }, // DBLV — PLL x4
		{ 0x15, 0x20 }, // COM10 — suppress PCLK during horizontal blanking
		{ REG_COM13, 0xC0 }, // gamma enable + UV auto-saturation (Linux YUV422 value)

		// END marker
		{0xFF, 0xFF},
};

/* ==========================================================================
 * MODE REGISTER SETS
 *
 * Each set fully defines COM3 / COM14 / COM7 / SCALING_* so that no stale
 * scaler state can survive a mode switch.  The hardware window (HSTART /
 * HSTOP / VSTART / VSTOP / HREF / VREF) is programmed separately by
 * ov7670_set_hw() in main.c, using the read-modify-write scheme from Linux.
 *
 * "Mystery scaling numbers" note: XSC=0x3A / YSC=0x35 come from OV's own
 * VGA init.  Bit 7 of XSC/YSC is the test-pattern enable, so the low 7 bits
 * (0x3A / 0x35) are what actually matter; they are the values OV ships for
 * the un-scaled modes and are also what the built-in CIF/QVGA/QCIF formats
 * expect.  Only the Linux "fake QCIF" path overrides them.
 * ========================================================================== */

/* --- Mode 0: VGA 640x480, no downsampling, no scaling ------------------- */
/* Full sensor FOV at the sensor's native maximum (the array is 656x488, so
 * 640x480 is as much as the chip can deliver).  Too big for SRAM => read as
 * strips.  Measured clean: no dummy columns at either edge, unlike CIF.
 *
 * CLKRC is /64 here rather than the /32 used elsewhere.  At /32 the pixel
 * clock is 2.6 MHz, leaving ~16 CPU cycles per PCLK half-phase, and the few
 * cycles between spotting HREF and entering the sampling loop could straddle
 * byte 0's high phase.  The resulting single missed edge rotated U/V for that
 * whole line and produced isolated magenta rows at strip starts.  /64 doubles
 * the margin; the only cost is frame time, which is irrelevant for stills. */
static const uint8_t ov7670_mode_vga[][2] = {
		{ REG_COM7,        COM7_FMT_VGA | COM7_YUV },  /* 0x00 */
		{ REG_COM3,        0x00 },                     /* no DCW, no scaling */
		{ REG_COM14,       0x00 },                     /* no PCLK divider     */
		{ REG_CLKRC,       0xBF },                     /* /64 — timing margin */
		{ REG_SCALING_XSC, 0x3A },
		{ REG_SCALING_YSC, 0x35 },
		{ 0x72,            0x11 },                     /* DCWCTR (inactive)   */
		{ 0x73,            0xF0 },                     /* PCLK_DIV = /1       */
		{ 0xA2,            0x02 },                     /* PCLK_DELAY          */
		{0xFF, 0xFF},
};

/* --- Mode 7: full active array 656x488 ---------------------------------- */
/* The OV7670 array is 656x488; the standard VGA window exposes only the inner
 * 640x480 of it.  Opening the window to the full array costs nothing but
 * buffer space, though the extra 16 columns / 8 rows are the sensor's dummy
 * and black-level reference pixels, so expect them to carry no picture.
 *
 * Window arithmetic: the line total is 784 pixels and the frame total 510
 * lines, and hstop/vstop wrap around those.  hstart=158 + 656 = 814 -> 30;
 * vstart=10 + 488 = 498. */
static const uint8_t ov7670_mode_vga_full[][2] = {
		{ REG_COM7,        COM7_FMT_VGA | COM7_YUV },
		{ REG_COM3,        0x00 },
		{ REG_COM14,       0x00 },
		{ REG_CLKRC,       0xBF },                     /* /64 — timing margin */
		{ REG_SCALING_XSC, 0x3A },
		{ REG_SCALING_YSC, 0x35 },
		{ 0x72,            0x11 },
		{ 0x73,            0xF0 },
		{ 0xA2,            0x02 },
		{0xFF, 0xFF},
};

/* --- Mode 1: CIF 352x288, built-in format ------------------------------ */
/* Exactly 2x QCIF in both axes => a 2x2 box average on the host produces a
 * pixel-perfect 176x144 with no resampling artifacts, at full sensor FOV. */
static const uint8_t ov7670_mode_cif[][2] = {
		{ REG_COM7,        COM7_FMT_CIF | COM7_YUV },  /* 0x20 */
		{ REG_COM3,        COM3_DCWEN },               /* 0x04 — DCW /2       */
		{ REG_COM14,       0x11 },                     /* DCW en + PCLK /2    */
		/* /64 rather than /32.  This stretches the horizontal blanking gap,
		 * which is where the snapshot path does its 2x2 averaging: at /32 the
		 * arithmetic overran the gap and stole the start of the next line
		 * (28 short lines out of 288).  The strip path only gains margin. */
		{ REG_CLKRC,       0xBF },
		{ REG_SCALING_XSC, 0x3A },
		{ REG_SCALING_YSC, 0x35 },
		{ 0x72,            0x11 },                     /* /2 in X and Y       */
		{ 0x73,            0xF1 },                     /* PCLK_DIV = /2       */
		{ 0xA2,            0x02 },
		{0xFF, 0xFF},
};

/* --- Mode 2: QVGA 320x240, built-in format ----------------------------- */
static const uint8_t ov7670_mode_qvga[][2] = {
		{ REG_COM7,        COM7_FMT_QVGA | COM7_YUV }, /* 0x10 */
		{ REG_COM3,        COM3_DCWEN },
		{ REG_COM14,       0x11 },
		{ REG_SCALING_XSC, 0x3A },
		{ REG_SCALING_YSC, 0x35 },
		{ 0x72,            0x11 },
		{ 0x73,            0xF1 },
		{ 0xA2,            0x02 },
		{0xFF, 0xFF},
};

/* --- Mode 3: QCIF 176x144 built-in (COM7=0x08) ------------------------- */
/* Full FOV but the sensor emits only 72 unique lines (see EXPERIMENT-LOG
 * section 2, experiments 1-3).  Kept for reference / regression checks. */
static const uint8_t ov7670_mode_qcif_builtin[][2] = {
		{ REG_COM7,        COM7_FMT_QCIF | COM7_YUV }, /* 0x08 */
		{ REG_COM3,        COM3_SCALEEN },             /* 0x08 */
		{ REG_COM14,       0x00 },
		{ REG_SCALING_XSC, 0x3A },
		{ REG_SCALING_YSC, 0x35 },
		{ 0x72,            0x11 },
		{ 0x73,            0xF0 },
		{ 0xA2,            0x02 },
		{0xFF, 0xFF},
};

/* --- Mode 4: Linux "fake QCIF" = VGA + scaler -------------------------- */
/* 144 unique lines but a cropped 352x480 window (hstart=456/hstop=24).
 * This was the previous firmware default. */
static const uint8_t ov7670_mode_qcif_linux[][2] = {
		{ REG_COM7,        COM7_FMT_VGA | COM7_YUV },  /* 0x00, see Linux note */
		{ REG_COM3,        COM3_SCALEEN | COM3_DCWEN },
		{ REG_COM3,        COM3_DCWEN },
		{ REG_COM14,       0x11 },
		{ 0x73,            0xF1 },
		{ 0xA2,            0x52 },
		{ 0x7B,            0x1C },
		{ 0x7C,            0x28 },
		{ 0x7D,            0x3C },
		{ 0x7F,            0x69 },
		{ REG_COM9,        0x38 },
		{ 0xA1,            0x0B },
		{ 0x74,            0x19 },
		{ 0x9A,            0x80 },
		{ 0x43,            0x14 },
		{ REG_COM13,       0xC0 },
		{0xFF, 0xFF},
};

/* ==========================================================================
 * MODE TABLE — ties each register set to its hardware window and the
 * nominal output geometry we expect from the sensor.
 *
 * hstart/hstop/vstart/vstop are the raw 11-bit / 10-bit values from
 * Linux ov7670_win_sizes[]; ov7670_set_hw() splits them across
 * HSTART/HSTOP/HREF and VSTART/VSTOP/VREF.
 *
 * Note hstop < hstart for several modes: the window wraps around the
 * 784-pixel total line length.  Width = (hstop - hstart) mod 784.
 *
 * ov7670_mode_t itself is declared in ov7670.h.
 * ========================================================================== */
#define OV_MODE_ENTRY(nm, arr, w, h, hs, he, vs, ve) \
    { nm, arr, (uint16_t)(sizeof(arr)/2), w, h, hs, he, vs, ve }

static const ov7670_mode_t ov7670_modes[] = {
    /*  0 */ OV_MODE_ENTRY("VGA",         ov7670_mode_vga,          640, 480, 158, 14, 10, 490),
    /*  1 */ OV_MODE_ENTRY("CIF",         ov7670_mode_cif,          352, 288, 170, 90, 14, 494),
    /*  2 */ OV_MODE_ENTRY("QVGA",        ov7670_mode_qvga,         320, 240, 168, 24, 12, 492),
    /*  3 */ OV_MODE_ENTRY("QCIF_BUILTIN",ov7670_mode_qcif_builtin, 176, 144, 158, 14, 10, 490),
    /*  4 */ OV_MODE_ENTRY("QCIF_LINUX",  ov7670_mode_qcif_linux,   176, 144, 456, 24, 14, 494),
    /*  5 */ OV_MODE_ENTRY("CIF_FULLWIN", ov7670_mode_cif,          352, 288, 158, 14, 10, 490),
    /*  6 */ OV_MODE_ENTRY("QVGA_FULLWIN",ov7670_mode_qvga,         320, 240, 158, 14, 10, 490),
    /*  7 */ OV_MODE_ENTRY("VGA_FULLARRAY", ov7670_mode_vga_full,  656, 488, 158, 30, 10, 498),
};
#define OV7670_MODE_COUNT (sizeof(ov7670_modes)/sizeof(ov7670_modes[0]))

#endif /* INC_CWSYS_CAMERA_BLOB_H_ */
