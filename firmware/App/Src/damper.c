/**
 * @file  damper.c
 * @brief Damper state machine.  See damper.h.
 */
#include "damper.h"

static damper_state_t  s_state;
static damper_source_t s_source;
static bool            s_locked;
static uint32_t        s_lock_start;

void Damper_Init(void)
{
    s_state  = DAMPER_CLOSED;
    s_source = DAMPER_SRC_NONE;
    s_locked = false;
    s_lock_start = 0;
}

uint32_t Damper_LockLeftMs(uint32_t now_ms)
{
    if (!s_locked) {
        return 0;
    }
    uint32_t passed = now_ms - s_lock_start;
    if (passed >= DAMPER_LOCK_MS) {
        s_locked = false;
        return 0;
    }
    return DAMPER_LOCK_MS - passed;
}

bool Damper_OnTemperature(int32_t temp_c100, uint32_t now_ms)
{
    if (Damper_LockLeftMs(now_ms) != 0) {
        return false;
    }

    if (s_state == DAMPER_CLOSED && temp_c100 > DAMPER_OPEN_ABOVE_C100) {
        s_state = DAMPER_OPEN;
    } else if (s_state == DAMPER_OPEN && temp_c100 <= DAMPER_CLOSE_BELOW_C100) {
        s_state = DAMPER_CLOSED;
    } else {
        return false;
    }
    s_source = DAMPER_SRC_TEMP;
    return true;
}

bool Damper_OnCameraTrigger(uint32_t now_ms)
{
    if (s_state == DAMPER_OPEN) {
        s_state = DAMPER_CLOSED;
    } else {
        s_state = DAMPER_OPEN;
    }
    s_source = DAMPER_SRC_CAMERA;
    s_locked = true;
    s_lock_start = now_ms;
    return true;
}

damper_state_t  Damper_State(void)      { return s_state; }
damper_source_t Damper_LastSource(void) { return s_source; }
