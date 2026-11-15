#include "fake_time.h"
#include "platform_time.h"
#include "bus_log.h"
#include <stdbool.h>

static uint32_t s_tick;
static bool     s_frozen;

void fake_time_reset(void) { s_tick = 0; s_frozen = false; }
uint32_t fake_time_now(void) { return s_tick; }
void fake_time_freeze(uint32_t now) { s_tick = now; s_frozen = true; }

void Platform_DelayMs(uint32_t ms)
{
    bus_log("DELAY %u;", (unsigned)ms);
    s_tick += ms;
}

uint32_t Platform_GetTick(void) { return s_frozen ? s_tick : s_tick++; }
