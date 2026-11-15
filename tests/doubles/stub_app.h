/* Stub of everything App/Src/app.c calls except the pure logic (damper, vision),
 * which stays real.  Calls are written to bus_log ("SERVO 90;", "CAM STRIP
 * 704 0 8;", ...), results and sensor values are set by the test. */
#ifndef STUB_APP_H
#define STUB_APP_H

#include <stdbool.h>
#include <stdint.h>
#include "ov7670_capture.h"

extern bool  stub_camera_ok;        /* self-test result for the camera */
extern bool  stub_flash_ok;         /* self-test result for the W25Q64 */
extern bool  stub_journal_init_ok;
extern bool  stub_journal_append_ok;

extern bool  stub_temp_ok;
extern float stub_temp;

extern bool  stub_button;
extern bool  stub_led;              /* last Platform_LedSet() */

/* Next captures: mean luma of the picture, return value (<0 = error), telemetry
 * (lines == 0xFFFFFFFF means "all requested lines arrived"), and how many
 * milliseconds of the clock the capture takes. */
extern uint8_t  stub_cam_luma;
extern int      stub_cam_ret;
extern ov7670_capture_stats_t stub_cam_stats;
extern uint32_t stub_cam_ms;
extern uint16_t stub_cam_width;     /* width of the current camera mode */

void stub_app_reset(void);

#endif
