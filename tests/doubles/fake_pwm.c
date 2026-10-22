#include "fake_pwm.h"
#include "platform_pwm.h"
#include "bus_log.h"

static uint16_t s_psc, s_arr, s_pulse;

void fake_pwm_reset(void) { s_psc = s_arr = s_pulse = 0; }
uint16_t fake_pwm_prescaler(void) { return s_psc; }
uint16_t fake_pwm_period(void) { return s_arr; }
uint16_t fake_pwm_pulse(void) { return s_pulse; }

void Platform_PWM_Init(uint16_t prescaler, uint16_t period)
{
    bus_log("PWMINIT %u %u;", prescaler, period);
    s_psc = prescaler;
    s_arr = period;
}

void Platform_PWM_SetPulse(uint16_t pulse_length)
{
    bus_log("PWM %u;", pulse_length);
    s_pulse = pulse_length;
}
