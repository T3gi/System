/**
 * @file  damper.h
 * @brief Damper state machine of the PRD: temperature control with hysteresis
 *        (FR-02, FR-03), camera override (FR-05) and the automation lock after
 *        an override (FR-06).  Pure logic: no hardware, no printing; App moves
 *        the servo and writes the journal when a call reports a change.
 */
#ifndef DAMPER_H
#define DAMPER_H

#include <stdbool.h>
#include <stdint.h>

/* Thresholds and lock time of the PRD.  Change here and rebuild. */
#define DAMPER_OPEN_ABOVE_C100   2600   /* FR-02: open  when T >  26.00 C */
#define DAMPER_CLOSE_BELOW_C100  2400   /* FR-03: close when T <= 24.00 C */
#define DAMPER_LOCK_MS           (5u * 60u * 1000u)   /* FR-06: 5 min */

typedef enum { DAMPER_CLOSED = 0, DAMPER_OPEN = 1 } damper_state_t;

/* Who changed the state last (logged with every change, FR-07). */
typedef enum {
    DAMPER_SRC_NONE   = 0,
    DAMPER_SRC_TEMP   = 1,
    DAMPER_SRC_CAMERA = 2,
} damper_source_t;

/** Closed, no lock. */
void Damper_Init(void);

/** New temperature in hundredths of a degree.  Ignored while locked.
 *  @return true if the state changed. */
bool Damper_OnTemperature(int32_t temp_c100, uint32_t now_ms);

/** Camera gesture: toggle the state and lock temperature control for
 *  DAMPER_LOCK_MS.  @return true (the state always changes). */
bool Damper_OnCameraTrigger(uint32_t now_ms);

damper_state_t  Damper_State(void);
damper_source_t Damper_LastSource(void);

/** Milliseconds of lock left, 0 when temperature control is active. */
uint32_t Damper_LockLeftMs(uint32_t now_ms);

#endif /* DAMPER_H */
