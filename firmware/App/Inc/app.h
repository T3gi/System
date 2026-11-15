/**
 * @file  app.h
 * @brief Application entry points called from main.c after the CubeMX init.
 */
#ifndef APP_H
#define APP_H

/** Boot self-test, camera mode setup, journal, READY banner. */
void App_Init(void);

/** One pass of the main loop: console, B1 button, temperature, camera,
 *  LED, 1 s heartbeat. */
void App_Poll(void);

/* ---- LR4: task bodies for the FreeRTOS build --------------------------
 * App_Poll() above calls each of them behind its period check; the RTOS
 * tasks (app_rtos.c) call them directly after vTaskDelayUntil(). */
#include <stdbool.h>
#include <stdint.h>

void App_TempRun(void);     /* FR-01..03: read temperature, damper decision */
void App_CameraRun(void);   /* FR-04..06: 8-line capture, gesture            */
void App_LedRun(void);      /* FR-08                                          */
void App_TickRun(void);     /* TICK heartbeat + temperature line               */
bool App_CameraOk(void);    /* camera answered at boot                         */
void Process_Button_Event(void);  /* EVT lines + latency, PC9 low               */

/* Hooks with weak defaults in app.c; app_rtos.c overrides them. */
void App_StateLock(void);
void App_StateUnlock(void);
bool App_JournalWrite(uint8_t state, uint8_t source, int16_t temp_c100, uint32_t t_ms);

#endif /* APP_H */
