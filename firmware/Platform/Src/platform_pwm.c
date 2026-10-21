#include "platform_pwm.h"
#include "stm32f4xx_hal.h"

extern TIM_HandleTypeDef htim3;

void Platform_PWM_Init(uint16_t prescaler, uint16_t period) {
    __HAL_TIM_SET_PRESCALER(&htim3, prescaler);
    __HAL_TIM_SET_AUTORELOAD(&htim3, period);

    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
}

void Platform_PWM_SetPulse(uint16_t pulse_length) {
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pulse_length);
}
