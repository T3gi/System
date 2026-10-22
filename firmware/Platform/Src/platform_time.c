#include "platform_time.h"
#include "stm32f4xx_hal.h"

void Platform_DelayMs(uint32_t ms) {
    HAL_Delay(ms);
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
