/* Fake platform_time: the tick advances by 1 ms on every read, so polling
 * loops with a timeout always terminate.  Delays are logged as "DELAY n;". */
#ifndef FAKE_TIME_H
#define FAKE_TIME_H

#include <stdint.h>

void fake_time_reset(void);
uint32_t fake_time_now(void);

#endif
