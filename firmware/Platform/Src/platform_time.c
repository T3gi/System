#include "platform_time.h"
#include "stm32f4xx_hal.h"

void Platform_DelayMs(uint32_t ms) {
    HAL_Delay(ms);
}

uint32_t Platform_GetTick(void) {
    return HAL_GetTick();
}
