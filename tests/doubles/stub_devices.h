/* Stub of the Devices layer for App tests: every driver call is written to
 * bus_log ("SCAN;", "W25Q64 ID;", ...) and returns the result set here, so
 * the application logic can be tested without any driver or bus behind it. */
#ifndef STUB_DEVICES_H
#define STUB_DEVICES_H

#include <stdbool.h>

extern bool stub_w25q64_ok;
extern bool stub_mpu6050_ok;
extern bool stub_gy89_ok;
extern bool stub_ov7670_ok;

/* Every device present and correct. */
void stub_devices_reset(void);

#endif
