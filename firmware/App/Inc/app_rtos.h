/**
 * @file  app_rtos.h
 * @brief LR4: the product as FreeRTOS tasks.  Task table, priorities and the
 *        reasons for them: docs/rtos-design.md.
 */
#ifndef APP_RTOS_H
#define APP_RTOS_H

#include <stdbool.h>
#include <stdint.h>

/** Create queues, mutexes, timers and tasks, then start the scheduler.
 *  Called from main() after App_Init(); never returns. */
void AppRtos_Start(void);

/** From EXTI15_10_IRQHandler (B1): wake the button task. */
void AppRtos_ButtonFromISR(void);

/* ---- for the measurement layer (rtos_metrics.c) ------------------------ */

/** Hooks with weak no-op defaults in app_rtos.c. */
void AppRtos_MetricsInit(void);    /* once, before the scheduler starts */
void AppRtos_MetricsTick(void);    /* once a second from the tick task  */

/** Handle of the long-operation task (TaskHandle_t, as void *). */
void *AppRtos_FrameTaskHandle(void);

/** Post one test record to the journal queue without waiting. */
bool AppRtos_JournalPostTest(void);
uint32_t AppRtos_JournalQueueLen(void);
void AppRtos_JournalStats(uint32_t *written, uint32_t *failed, uint32_t *test,
                          uint32_t *btn_ignored);

#endif /* APP_RTOS_H */
