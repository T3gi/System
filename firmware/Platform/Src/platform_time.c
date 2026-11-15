#include "platform_time.h"
#include "stm32f4xx_hal.h"
#include "platform_rtos.h"
#include "FreeRTOS.h"
#include "task.h"

/* LR4: inside a task the wait blocks and lets other tasks run; before the
 * scheduler starts (boot self-test) it is the HAL busy wait, as in LR3. */
void Platform_DelayMs(uint32_t ms) {
    if (Platform_RtosRunning()) {
        TickType_t t = pdMS_TO_TICKS(ms);
        vTaskDelay(t > 0 ? t : 1);
    } else {
        HAL_Delay(ms);
    }
}

uint32_t Platform_GetTick(void) {
    return HAL_GetTick();
}

void Platform_CycleCounterStart(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t Platform_CycleCount(void) {
    return DWT->CYCCNT;
}

uint32_t Platform_CyclesPerUs(void) {
    return SystemCoreClock / 1000000u;
}
