#ifndef PLATFORM_TIME_H
#define PLATFORM_TIME_H

#include <stdint.h>

void Platform_DelayMs(uint32_t ms);

uint32_t Platform_GetTick(void);

/* CPU cycle counter (DWT) for timing short operations. */
void     Platform_CycleCounterStart(void);
uint32_t Platform_CycleCount(void);
uint32_t Platform_CyclesPerUs(void);

#endif
