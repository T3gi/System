#include "servo.h"
#include "platform_pwm.h"

void Servo_Init(void) {
    /* TIM3 runs at 84 MHz: PSC = 83 -> 1 us tick, ARR = 19999 -> 20 ms. */
    Platform_PWM_Init(84 - 1, 20000 - 1);

    Platform_PWM_SetPulse(1500);
}

void Servo_SetAngle(uint8_t angle) {
    if (angle > 180) angle = 180;
    /* 0° = ~1000 мкс (1.0 мс), 180° = ~2000 мкс (2.0 мс) */
    uint32_t pulse = 1000 + ((uint32_t)angle * 1000) / 180;
    Platform_PWM_SetPulse((uint16_t)pulse);
}
