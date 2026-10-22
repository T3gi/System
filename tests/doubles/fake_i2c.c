#include "fake_i2c.h"
#include "platform_i2c.h"
#include "bus_log.h"
#include <string.h>

static uint8_t s_regs[256][256];
static bool    s_present[256];
static bool    s_probed[256];
static int     s_probes;
static int     s_fail_next;
static int     s_fail_dev = -1, s_fail_reg = -1;

void fake_i2c_reset(void)
{
    memset(s_regs, 0, sizeof s_regs);
    memset(s_present, 0, sizeof s_present);
    memset(s_probed, 0, sizeof s_probed);
    s_probes = 0;
    s_fail_next = 0;
    s_fail_dev = s_fail_reg = -1;
}

void fake_i2c_add(uint8_t dev8) { s_present[dev8] = true; }

void fake_i2c_set_reg(uint8_t dev8, uint8_t reg, uint8_t val) { s_regs[dev8][reg] = val; }

void fake_i2c_set_regs(uint8_t dev8, uint8_t reg, const uint8_t *val, unsigned n)
{
    for (unsigned i = 0; i < n; i++) s_regs[dev8][(uint8_t)(reg + i)] = val[i];
}

uint8_t fake_i2c_reg(uint8_t dev8, uint8_t reg) { return s_regs[dev8][reg]; }

void fake_i2c_fail_next(int n) { s_fail_next = n; }

void fake_i2c_fail_reg(uint8_t dev8, uint8_t reg)
{
    s_fail_dev = dev8;
    s_fail_reg = reg;
}

int fake_i2c_probe_count(void) { return s_probes; }
bool fake_i2c_was_probed(uint8_t dev8) { return s_probed[dev8]; }

static bool transfer_ok(uint8_t dev8, uint8_t reg)
{
    if (s_fail_next > 0) { s_fail_next--; return false; }
    if (dev8 == s_fail_dev && reg == s_fail_reg) return false;
    return s_present[dev8];
}

bool Platform_I2C_ReadReg(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    bus_log("I2C R %02X %02X %u;", dev_addr, reg_addr, len);
    if (!transfer_ok(dev_addr, reg_addr)) return false;
    /* The chip auto-increments the register pointer on a multi-byte read. */
    for (uint16_t i = 0; i < len; i++) data[i] = s_regs[dev_addr][(uint8_t)(reg_addr + i)];
    return true;
}

bool Platform_I2C_WriteReg(uint8_t dev_addr, uint8_t reg_addr, const uint8_t *data, uint16_t len)
{
    bus_log("I2C W %02X %02X", dev_addr, reg_addr);
    for (uint16_t i = 0; i < len; i++) bus_log(" %02X", data[i]);
    bus_log(";");
    if (!transfer_ok(dev_addr, reg_addr)) return false;
    for (uint16_t i = 0; i < len; i++) s_regs[dev_addr][(uint8_t)(reg_addr + i)] = data[i];
    return true;
}

bool Platform_I2C_IsDeviceReady(uint8_t dev_addr, uint32_t trials, uint32_t timeout_ms)
{
    (void)trials;
    (void)timeout_ms;
    s_probes++;
    s_probed[dev_addr] = true;
    return s_present[dev_addr];
}
