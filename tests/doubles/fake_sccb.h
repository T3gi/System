/* Fake SCCB bus with an OV7670 register file (implements the two functions of
 * platform_sccb.h the camera driver uses).
 *
 * Every access is recorded in the same form as a decoded capture
 * (vectors/sccb_vector.h): a register write is one 3-phase write, a register
 * read is a 2-phase write followed by a 2-phase read.  That lets a test compare
 * the driver's traffic with the logic-analyzer vector entry by entry. */
#ifndef FAKE_SCCB_H
#define FAKE_SCCB_H

#include <stdbool.h>
#include <stdint.h>
#include "sccb_vector.h"

void fake_sccb_reset(void);
void fake_sccb_set_reg(uint8_t reg, uint8_t val);
uint8_t fake_sccb_reg(uint8_t reg);
/* sccb_read_reg() returns -1 (bus error after all retries) from now on. */
void fake_sccb_fail_reads(bool on);

unsigned fake_sccb_count(void);
const sccb_vec_t *fake_sccb_trace(void);
/* Slave address of the last access (the driver must always use 0x21). */
uint8_t fake_sccb_last_slave(void);

#endif
