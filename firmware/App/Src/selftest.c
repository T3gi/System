/**
 * @file  selftest.c
 * @brief Boot self-test, moved from main.c.  See selftest.h for the format.
 */
#include "selftest.h"
#include "i2c_scan.h"
#include "servo.h"
#include "w25q64.h"
#include "mpu6050.h"
#include "gy89.h"
#include "ov7670_capture.h"
#include "platform_time.h"
#include <stdio.h>

static bool report(selftest_result_t *r, const char *name, bool ok)
{
    printf("SELFTEST %s %s\r\n", name, ok ? "OK" : "FAIL");
    r->total++;
    if (ok) r->passed++;
    return ok;
}

selftest_result_t SelfTest_Run(void)
{
    selftest_result_t r = {0};

    printf("\r\n=== Запуск перевірки компонентів ===\r\n");

    I2C_Scan();

    /* The servo has no feedback, so the sweep is only watched by eye and is
     * not part of the summary. */
    Servo_Init();
    Servo_SetAngle(0);
    Platform_DelayMs(1000);
    Servo_SetAngle(90);
    Platform_DelayMs(1000);
    Servo_SetAngle(180);
    Platform_DelayMs(1000);

    r.w25q64  = report(&r, "W25Q64",  W25Q64_ReadID());
    r.mpu6050 = report(&r, "MPU6050", MPU6050_Init());
    r.gy89    = report(&r, "GY89",    GY89_ReadIDs());

    printf("===================================\r\n");

    OV7670_PowerUp();
    r.ov7670  = report(&r, "OV7670",  OV7670_Probe());

    printf("SELFTEST SUMMARY %d/%d OK\r\n", r.passed, r.total);
    return r;
}
