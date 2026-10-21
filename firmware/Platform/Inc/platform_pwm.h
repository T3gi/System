#ifndef PLATFORM_PWM_H
#define PLATFORM_PWM_H

#include <stdint.h>

/* TIM3_CH1 on PA6: set PSC and ARR, then start the PWM output. */
void Platform_PWM_Init(uint16_t prescaler, uint16_t period);

void Platform_PWM_SetPulse(uint16_t pulse_length);

#endif
