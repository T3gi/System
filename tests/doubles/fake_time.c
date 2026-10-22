#include "fake_time.h"
#include "platform_time.h"
#include "bus_log.h"

static uint32_t s_tick;

void fake_time_reset(void) { s_tick = 0; }
uint32_t fake_time_now(void) { return s_tick; }

void Platform_DelayMs(uint32_t ms)
{
    bus_log("DELAY %u;", (unsigned)ms);
    s_tick += ms;
}

uint32_t Platform_GetTick(void) { return s_tick++; }
