/* Fake PWM timer (implements platform_pwm.h): logs "PWMINIT psc arr;" and
 * "PWM pulse;" and remembers the timer setup. */
#ifndef FAKE_PWM_H
#define FAKE_PWM_H

#include <stdint.h>

void fake_pwm_reset(void);
uint16_t fake_pwm_prescaler(void);
uint16_t fake_pwm_period(void);
uint16_t fake_pwm_pulse(void);

#endif
