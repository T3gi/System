/* Fake platform_time: the tick advances by 1 ms on every read, so polling
 * loops with a timeout always terminate.  Delays are logged as "DELAY n;".
 * Tests of the main loop stop the clock at a chosen moment instead. */
#ifndef FAKE_TIME_H
#define FAKE_TIME_H

#include <stdint.h>

void fake_time_reset(void);
uint32_t fake_time_now(void);
/* Set the clock; from here on reads do not advance it (reset undoes this). */
void fake_time_freeze(uint32_t now);

#endif
