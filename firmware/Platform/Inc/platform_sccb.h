/**
 * @file  sccb.h
 * @brief OV7670 SCCB access on STM32F401, PB8 = SIO_C (SCL), PB9 = SIO_D (SDA).
 *
 * Goal of this module: talk SCCB with as much stock HAL I2C as possible and
 * compare against a hand-written bit-bang reference.
 *
 * SCCB vs I2C — what matters for an STM32 HAL master:
 *   1. 9th bit is "Don't-Care" (X), not ACK.  HAL aborts with
 *      HAL_I2C_ERROR_AF if the slave leaves SDA high there.  In practice the
 *      OV7670 does pull it low on write phases — that is what lets HAL work.
 *   2. No repeated START.  An SCCB read is [S addr+W reg P] [S addr+R data NA P].
 *      HAL_I2C_Mem_Read sends S..Sr.. (no STOP) — not defined by SCCB.
 *      HAL_I2C_Master_Transmit + HAL_I2C_Master_Receive produce exactly the
 *      SCCB sequence (each call ends with STOP).
 *   3. Master ends a read with NA (SDA high) — identical to I2C NACK, which
 *      HAL_I2C_Master_Receive of 1 byte does automatically.
 *   4. Max 400 kHz, no clock stretching, sensor needs XCLK + PWDN low.
 *
 * Back-ends (switchable at run time):
 *   SCCB_HAL      — write: HAL_I2C_Mem_Write; read: Master_Transmit + Master_Receive.
 *                   The pattern used by iwatake2222/DigitalCamera_STM32 (ov7670.c)
 *                   and the electronics.stackexchange #466712 answer.
 *   SCCB_HAL_MEM  — write + read both with HAL_I2C_Mem_* (plain I2C, Sr on read).
 *                   Control case: shows what breaks when SCCB is treated as I2C.
 *   SCCB_BITBANG  — GPIO open-drain reference; ignores the X bit but records it.
 */
#ifndef PLATFORM_SCCB_H
#define PLATFORM_SCCB_H

#include <stdint.h>

typedef enum {
    SCCB_HAL      = 0,
    SCCB_HAL_MEM  = 1,
    SCCB_BITBANG  = 2,
    SCCB_BACKEND_COUNT
} sccb_backend_t;

typedef struct {
    uint32_t xfers;      /* register reads + writes attempted                 */
    uint32_t errors;     /* transfers that failed after all retries           */
    uint32_t retries;    /* extra attempts that were needed                   */
    uint32_t x_low;      /* bit-bang only: 9th bit seen LOW  (slave "ACKed")  */
    uint32_t x_high;     /* bit-bang only: 9th bit seen HIGH (slave silent)   */
    uint32_t last_err;   /* last HAL ErrorCode (HAL_I2C_ERROR_*)              */
} sccb_stats_t;

/** Stop I2C1 and park PB8/PB9 as open-drain GPIO with pull-ups. */
void sccb_pins_reinit(void);

/** Switch pins/peripheral to a back-end.  0 = OK, <0 = error. */
int  sccb_init(sccb_backend_t b);
sccb_backend_t sccb_backend(void);
const char *sccb_backend_name(sccb_backend_t b);

/** Attempts per transfer for the HAL back-ends (1 = no retry). Default 1. */
void sccb_set_attempts(int n);
/** HAL back-ends: wait for CR1.STOP to clear after each call (default 1). */
void sccb_set_stop_wait(int on);

/** Read one register: 0..255, or negative on error. */
int  sccb_read_reg(uint8_t slave7, uint8_t reg);
/** Write one register: 0 = OK, negative on error. */
int  sccb_write_reg(uint8_t slave7, uint8_t reg, uint8_t val);

void sccb_stats_reset(void);
const sccb_stats_t *sccb_stats(void);

#endif /* PLATFORM_SCCB_H */
