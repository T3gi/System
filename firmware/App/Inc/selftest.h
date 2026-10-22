/**
 * @file  selftest.h
 * @brief Boot self-test (ЛР2 §4.2): checks every device and prints one
 *        machine-parsable line per device plus a summary:
 *
 *          SELFTEST <DEVICE> OK|FAIL
 *          SELFTEST SUMMARY <passed>/<total> OK
 *
 *        The drivers' own human-readable lines are printed above them.
 */
#ifndef SELFTEST_H
#define SELFTEST_H

#include <stdbool.h>

typedef struct {
    bool w25q64;
    bool mpu6050;
    bool gy89;
    bool ov7670;
    int  passed;
    int  total;
} selftest_result_t;

/** Run all checks in the boot order (I2C sensors first, then the camera,
 *  whose SCCB shares the I2C1 pins). */
selftest_result_t SelfTest_Run(void);

#endif /* SELFTEST_H */
