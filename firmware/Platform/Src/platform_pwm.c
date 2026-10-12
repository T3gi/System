#include "platform_pwm.h"
#include "stm32f4xx_hal.h"

extern TIM_HandleTypeDef htim3;

void Platform_PWM_SetPulse(uint16_t pulse_length) {
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pulse_length);
}