/**
 * @file  sccb.c
 * @brief OV7670 SCCB master: stock HAL I2C back-ends + bit-bang reference.
 *        See sccb.h for the SCCB-vs-I2C notes and back-end description.
 */
#include "sccb.h"
#include "stm32f4xx_hal.h"

#define SCL_PIN        GPIO_PIN_8
#define SDA_PIN        GPIO_PIN_9
#define SCCB_PORT      GPIOB
#define SCCB_I2C_HZ    100000u      /* SCCB allows up to 400 kHz            */
#define HAL_TIMEOUT_MS 10u

static I2C_HandleTypeDef s_hi2c;
static sccb_backend_t    s_backend = SCCB_HAL;
static sccb_stats_t      s_st;
static int               s_attempts = 3;   /* normal operation: retry up to 2x */

/* ======================================================================= */
/* Pin helpers                                                              */
/* ======================================================================= */
static void pins_gpio_od(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin   = SCL_PIN | SDA_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_OD;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_WritePin(SCCB_PORT, SCL_PIN | SDA_PIN, GPIO_PIN_SET);
    HAL_GPIO_Init(SCCB_PORT, &g);
}

static inline void bb_delay(void)
{
    /* ~5 us half-bit @ 84 MHz -> ~50 kHz SIO_C.  Same as the lab2 driver. */
    for (volatile int i = 0; i < 400; i++) __NOP();
}

/* Clock out a stuck slave and leave the bus idle (SCL=SDA=1) with a STOP.
 * Needed when switching from bit-bang to the I2C peripheral, otherwise the
 * F4 I2C can latch BUSY=1 (errata 2.14.7) and every HAL call times out. */
static void bus_recover(void)
{
    pins_gpio_od();
    for (int i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(SCCB_PORT, SCL_PIN, GPIO_PIN_RESET); bb_delay();
        HAL_GPIO_WritePin(SCCB_PORT, SCL_PIN, GPIO_PIN_SET);   bb_delay();
    }
    HAL_GPIO_WritePin(SCCB_PORT, SDA_PIN, GPIO_PIN_RESET); bb_delay();
    HAL_GPIO_WritePin(SCCB_PORT, SDA_PIN, GPIO_PIN_SET);   bb_delay();
}

/* ======================================================================= */
/* HAL I2C setup                                                            */
/* ======================================================================= */

static int hal_i2c_start(void)
{
    if (s_hi2c.Instance) HAL_I2C_DeInit(&s_hi2c);
    bus_recover();
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_I2C1_FORCE_RESET();
    __HAL_RCC_I2C1_RELEASE_RESET();

    s_hi2c.Instance             = I2C1;
    s_hi2c.Init.ClockSpeed      = SCCB_I2C_HZ;
    s_hi2c.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    s_hi2c.Init.OwnAddress1     = 0;
    s_hi2c.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    s_hi2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_hi2c.Init.OwnAddress2     = 0;
    s_hi2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_hi2c.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    s_hi2c.State                = HAL_I2C_STATE_RESET;   /* force MspInit */
    return (HAL_I2C_Init(&s_hi2c) == HAL_OK) ? 0 : -1;
}

/* Any HAL failure: record it and re-initialise the peripheral.  Without this
 * a single HAL_I2C_WRONG_START leaves CR1.START latched and every following
 * transfer fails (observed: 5 OK, then 195 errors in a row). */
static void hal_after_error(void)
{
    s_st.last_err = HAL_I2C_GetError(&s_hi2c);
    (void)hal_i2c_start();
}

/* F4 HAL returns from Master_Transmit/Receive as soon as it has *requested*
 * the STOP (CR1.STOP=1), not when the STOP is on the bus.  If the next call
 * sets CR1.START while STOP is still pending, SB never comes and HAL reports
 * HAL_I2C_WRONG_START.  Waiting for hardware to clear CR1.STOP closes that
 * race.  This is the only register access in the HAL back-end. */
static int s_stop_wait = 1;
static void hal_wait_stop(void)
{
    if (!s_stop_wait) return;
    for (uint32_t i = 0; i < 100000u && (I2C1->CR1 & I2C_CR1_STOP); i++) { }
}
void sccb_set_stop_wait(int on) { s_stop_wait = on; }

/* ---- the whole "driver" for the HAL back-ends is these few lines ------- */
static int hal_write(uint8_t a7, uint8_t reg, uint8_t val)
{
    int r = HAL_I2C_Mem_Write(&s_hi2c, (uint16_t)(a7 << 1), reg,
                              I2C_MEMADD_SIZE_8BIT, &val, 1, HAL_TIMEOUT_MS)
            == HAL_OK ? 0 : -1;
    hal_wait_stop();
    return r;
}

static int hal_read_seq(uint8_t a7, uint8_t reg)
{
    uint8_t v;
    /* SCCB 2-phase write (sets register pointer, ends with STOP) ...      */
    if (HAL_I2C_Master_Transmit(&s_hi2c, (uint16_t)(a7 << 1), &reg, 1,
                                HAL_TIMEOUT_MS) != HAL_OK) return -1;
    hal_wait_stop();
    /* ... then SCCB 2-phase read (1 byte -> HAL sends NA + STOP).         */
    if (HAL_I2C_Master_Receive(&s_hi2c, (uint16_t)(a7 << 1), &v, 1,
                               HAL_TIMEOUT_MS) != HAL_OK) return -1;
    hal_wait_stop();
    return v;
}

static int hal_read_mem(uint8_t a7, uint8_t reg)
{
    uint8_t v;
    int r = HAL_I2C_Mem_Read(&s_hi2c, (uint16_t)(a7 << 1), reg,
                             I2C_MEMADD_SIZE_8BIT, &v, 1, HAL_TIMEOUT_MS);
    hal_wait_stop();
    return (r == HAL_OK) ? v : -1;
}

/* ======================================================================= */
/* Bit-bang reference (same timing as the lab2 driver, plus X-bit sampling) */
/* ======================================================================= */
#define SCL_H() (SCCB_PORT->BSRR = SCL_PIN)
#define SCL_L() (SCCB_PORT->BSRR = (uint32_t)SCL_PIN << 16)
#define SDA_H() (SCCB_PORT->BSRR = SDA_PIN)
#define SDA_L() (SCCB_PORT->BSRR = (uint32_t)SDA_PIN << 16)
#define SDA_R() ((SCCB_PORT->IDR & SDA_PIN) ? 1 : 0)

static void bb_start(void)
{
    SDA_H(); SCL_H(); bb_delay();
    SDA_L(); bb_delay();
    SCL_L(); bb_delay();
}

static void bb_stop(void)
{
    SCL_L(); bb_delay();
    SDA_L(); bb_delay();
    SCL_H(); bb_delay();
    SDA_H(); bb_delay();
}

static void bb_write_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        if (b & (1u << i)) SDA_H(); else SDA_L();
        bb_delay();
        SCL_H(); bb_delay();
        SCL_L(); bb_delay();
    }
    /* 9th bit = Don't-Care: release SDA, clock it, record what the slave did. */
    SDA_H(); bb_delay();
    SCL_H(); bb_delay();
    if (SDA_R()) s_st.x_high++; else s_st.x_low++;
    SCL_L(); bb_delay();
}

static uint8_t bb_read_byte(void)
{
    uint8_t v = 0;
    SDA_H(); bb_delay();
    for (int i = 7; i >= 0; i--) {
        SCL_H(); bb_delay();
        v = (uint8_t)((v << 1) | SDA_R());
        SCL_L(); bb_delay();
    }
    /* NA bit: master leaves SDA high. */
    SDA_H(); bb_delay();
    SCL_H(); bb_delay();
    SCL_L(); bb_delay();
    return v;
}

static int bb_write(uint8_t a7, uint8_t reg, uint8_t val)
{
    bb_start();
    bb_write_byte((uint8_t)(a7 << 1));
    bb_write_byte(reg);
    bb_write_byte(val);
    bb_stop();
    return 0;
}

static int bb_read(uint8_t a7, uint8_t reg)
{
    bb_start();
    bb_write_byte((uint8_t)(a7 << 1));
    bb_write_byte(reg);
    bb_stop();                       /* SCCB: STOP, not repeated START */
    bb_start();
    bb_write_byte((uint8_t)((a7 << 1) | 1));
    uint8_t v = bb_read_byte();
    bb_stop();
    return v;
}

/* ======================================================================= */
/* Public API                                                               */
/* ======================================================================= */
int sccb_init(sccb_backend_t b)
{
    if (b >= SCCB_BACKEND_COUNT) return -2;
    s_backend = b;
    if (b == SCCB_BITBANG) {
        if (s_hi2c.Instance) { HAL_I2C_DeInit(&s_hi2c); s_hi2c.Instance = 0; }
        bus_recover();
        return 0;
    }
    return hal_i2c_start();
}

sccb_backend_t sccb_backend(void) { return s_backend; }

const char *sccb_backend_name(sccb_backend_t b)
{
    switch (b) {
    case SCCB_HAL:     return "HAL (Mem_Write + Transmit/Receive)";
    case SCCB_HAL_MEM: return "HAL_MEM (Mem_Write + Mem_Read)";
    case SCCB_BITBANG: return "BITBANG (GPIO reference)";
    default:           return "?";
    }
}

void sccb_set_attempts(int n) { s_attempts = (n < 1) ? 1 : n; }

int sccb_read_reg(uint8_t slave7, uint8_t reg)
{
    s_st.xfers++;
    if (s_backend == SCCB_BITBANG) return bb_read(slave7, reg);

    for (int a = 0; a < s_attempts; a++) {
        if (a) s_st.retries++;
        int v = (s_backend == SCCB_HAL) ? hal_read_seq(slave7, reg)
                                        : hal_read_mem(slave7, reg);
        if (v >= 0) return v;
        hal_after_error();
    }
    s_st.errors++;
    return -1;
}

int sccb_write_reg(uint8_t slave7, uint8_t reg, uint8_t val)
{
    s_st.xfers++;
    if (s_backend == SCCB_BITBANG) return bb_write(slave7, reg, val);

    for (int a = 0; a < s_attempts; a++) {
        if (a) s_st.retries++;
        if (hal_write(slave7, reg, val) == 0) return 0;
        hal_after_error();
    }
    s_st.errors++;
    return -1;
}

void sccb_stats_reset(void)
{
    sccb_stats_t z = {0};
    s_st = z;
}

const sccb_stats_t *sccb_stats(void) { return &s_st; }
