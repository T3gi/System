#include "fake_sccb.h"
#include "platform_sccb.h"
#include "bus_log.h"
#include <string.h>

#define MAX_TRACE 1024

static uint8_t    s_regs[256];
static sccb_vec_t s_trace[MAX_TRACE];
static unsigned   s_n;
static bool       s_fail_reads;
static uint8_t    s_slave;

void fake_sccb_reset(void)
{
    memset(s_regs, 0, sizeof s_regs);
    s_n = 0;
    s_fail_reads = false;
    s_slave = 0;
}

void fake_sccb_set_reg(uint8_t reg, uint8_t val) { s_regs[reg] = val; }
uint8_t fake_sccb_reg(uint8_t reg) { return s_regs[reg]; }
void fake_sccb_fail_reads(bool on) { s_fail_reads = on; }
unsigned fake_sccb_count(void) { return s_n; }
const sccb_vec_t *fake_sccb_trace(void) { return s_trace; }
uint8_t fake_sccb_last_slave(void) { return s_slave; }

static void trace(sccb_vec_op_t op, uint8_t reg, uint8_t val)
{
    if (s_n < MAX_TRACE) s_trace[s_n++] = (sccb_vec_t){ op, reg, val };
}

int sccb_read_reg(uint8_t slave7, uint8_t reg)
{
    s_slave = slave7;
    bus_log("SCCB R %02X %02X;", slave7, reg);
    trace(SCCB_W2, reg, 0x00);
    if (s_fail_reads) return -1;
    trace(SCCB_R, 0x00, s_regs[reg]);
    return s_regs[reg];
}

int sccb_write_reg(uint8_t slave7, uint8_t reg, uint8_t val)
{
    s_slave = slave7;
    bus_log("SCCB W %02X %02X %02X;", slave7, reg, val);
    trace(SCCB_W3, reg, val);
    s_regs[reg] = val;
    return 0;
}
