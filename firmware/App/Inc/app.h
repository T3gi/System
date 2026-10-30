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

#endif /* APP_H */
