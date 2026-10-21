#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>

/* 1 us timer tick, 20 ms period, start at 90 degrees (1.5 ms). */
void Servo_Init(void);

/* 0..180 degrees -> 1.0..2.0 ms pulse; larger values are clamped to 180. */
void Servo_SetAngle(uint8_t angle);

#endif
