/**
 * @file  app.c
 * @brief Boot sequence and main loop, moved from main.c.
 */
#include "app.h"
#include "selftest.h"
#include "console.h"
#include "ov7670.h"
#include "mpu6050.h"
#include "platform_time.h"
#include "platform_board.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t s_last_blink;
static bool     s_btn_prev;

void App_Init(void)
{
    selftest_result_t st = SelfTest_Run();

    if (st.ov7670) {
        ov7670_configure(ov7670_current_mode());
        Platform_DelayMs(1200);            /* AWB/AEC convergence */
    } else {
        printf("ERR OV7670 not detected on SCCB\r\n");
    }

    printf("READY\r\n");
    Console_Help();
    printf("OK\r\n");

    /* LD2 blinks slowly so it is obvious the rig is alive. */
    s_last_blink = Platform_GetTick();
    s_btn_prev   = false;
}

static void print_temperature(void)
{
    float temp = 0.0f;

    if (MPU6050_ReadTemperature(&temp)) {
        int32_t int_part = (int32_t)temp;
        // Отримуємо 2 знаки після коми (множимо на 100)
        int32_t frac_part = (int32_t)((temp - (float)int_part) * 100.0f);

        // Враховуємо випадок від'ємних температур
        if (frac_part < 0) {
            frac_part = -frac_part;
        }

        printf("[MPU6050] Температура: %ld.%02ld °C\r\n", (long)int_part, (long)frac_part);
    } else {
        printf("[MPU6050] Помилка отримання температури!\r\n");
    }
}

void App_Poll(void)
{
    Console_Poll();

    /* USER button still works as a manual trigger: grab a full frame of
     * the current mode using as many strips as needed. */
    bool btn = Platform_ButtonPressed();
    if (btn && !s_btn_prev) {
        Console_CaptureFrame();
    }
    s_btn_prev = btn;

    if (Platform_GetTick() - s_last_blink > 1000) {
        s_last_blink = Platform_GetTick();
        Platform_LedToggle();
        /* Idle heartbeat.  Printed only from the command loop, so it can
         * never interleave with a strip payload.  If these stop while the
         * board is otherwise silent, the main loop itself is stuck; if
         * they continue but commands go unanswered, the RX path is at
         * fault.  Distinguishing those two took far too long without it. */
        printf("TICK t=%lu rx=%lu ore=%lu cmds=%lu\r\n",
               (unsigned long)s_last_blink,
               (unsigned long)Platform_Console_RxBytes(),
               (unsigned long)Platform_Console_RxOverruns(),
               (unsigned long)Console_CommandCount());
        print_temperature();
    }
}
