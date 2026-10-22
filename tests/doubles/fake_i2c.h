/* Fake I2C bus (implements platform_i2c.h).
 *
 * A fake is a small working model: every 8-bit address has a register file,
 * reads and writes really change it, and a missing device answers NACK.  Tests
 * set the registers up, call the driver, then check the result and the log.
 *
 * Platform_I2C_* return false for any HAL error, so a NACK and a timeout look
 * the same to the drivers; fake_i2c_fail_next() injects that failure. */
#ifndef FAKE_I2C_H
#define FAKE_I2C_H

#include <stdbool.h>
#include <stdint.h>

void fake_i2c_reset(void);
/* Make a device answer at this 8-bit (shifted) address. */
void fake_i2c_add(uint8_t dev8);
void fake_i2c_set_reg(uint8_t dev8, uint8_t reg, uint8_t val);
void fake_i2c_set_regs(uint8_t dev8, uint8_t reg, const uint8_t *val, unsigned n);
uint8_t fake_i2c_reg(uint8_t dev8, uint8_t reg);
/* The next n read/write transactions fail (NACK or timeout). */
void fake_i2c_fail_next(int n);
/* Every transaction that touches this register fails. */
void fake_i2c_fail_reg(uint8_t dev8, uint8_t reg);

int  fake_i2c_probe_count(void);
bool fake_i2c_was_probed(uint8_t dev8);

#endif
