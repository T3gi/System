/**
 * @file  app_rtos.c
 * @brief LR4: the LR3 product as FreeRTOS tasks.
 *
 * Task table (priority: higher number = more urgent; docs/rtos-design.md):
 *
 *   task     prio  start                       body (from app.c / console.c)
 *   btn       6    notify from EXTI (B1)       Process_Button_Event, start frame
 *   temp      4    vTaskDelayUntil 1000 ms     App_TempRun: FR-01..03
 *   tick      3    vTaskDelayUntil 1000 ms     App_TickRun (+ LOAD, metrics)
 *   cmd       3    notify per received byte    Console_Poll: commands
 *   camera    2    500 ms after each capture   App_CameraRun: FR-04..06
 *   journal   2    queue from the damper logic Journal_Append: FR-07
 *   Tmr Svc   2    kernel                      LED timer, 20 ms: FR-08
 *   frame     1    notify from btn             Console_CaptureFrame (≈31 s)
 *   IDLE      0    kernel                      idle time = free CPU
 *
 * ISRs only record the event and wake a task (USART2 RX, EXTI B1).  Data
 * between tasks goes through a queue (journal) or a notification; the damper
 * state shared by temp and camera is under a mutex; the buses are locked in
 * the Platform layer.
 *
 * The OS measurement commands and the LR4 scenario fixtures live in
 * rtos_metrics.c; they attach through the two weak hooks below.
 */
#include "app_rtos.h"
#include "app.h"
#include "console.h"
#include "journal.h"
#include "platform_board.h"
#include "platform_rtos.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"

#include <stdio.h>

/* ---- priorities and stack sizes (words) --------------------------------- *
 * Stack sizes start from the static estimate (.su files) plus printf, then
 * are checked with the STACK command after S3.  Each can be overridden at
 * build time for the stack-overflow scenario, e.g. -DSTACK_TICK=48.        */
#define PRIO_BTN      6
#define PRIO_TEMP     4
#define PRIO_TICK     3
#define PRIO_CMD      3
#define PRIO_CAMERA   2
#define PRIO_JOURNAL  2
#define PRIO_FRAME    1

#ifndef STACK_BTN
#define STACK_BTN     256
#endif
#ifndef STACK_TEMP
#define STACK_TEMP    320
#endif
#ifndef STACK_TICK
#define STACK_TICK    320
#endif
#ifndef STACK_CMD
#define STACK_CMD     768
#endif
#ifndef STACK_CAMERA
#define STACK_CAMERA  320
#endif
#ifndef STACK_JOURNAL
#define STACK_JOURNAL 448
#endif
#ifndef STACK_FRAME
#define STACK_FRAME   448
#endif

#define TEMP_PERIOD_MS    1000   /* FR-01 */
#define TICK_PERIOD_MS    1000
#define CAMERA_PAUSE_MS    500   /* as in LR3: pause after each capture */
#define LED_PERIOD_MS       20   /* FR-08: LED follows the state within 50 ms */
#define JOURNAL_QUEUE_LEN    8

/* ---- kernel objects ------------------------------------------------------ */
static TaskHandle_t h_btn, h_temp, h_tick, h_cmd, h_camera, h_journal, h_frame;
static SemaphoreHandle_t s_state_mutex;   /* damper/vision state (temp + camera) */
static QueueHandle_t     s_journal_q;     /* damper changes -> journal task */
static TimerHandle_t     s_led_timer;

static volatile bool     s_frame_busy;
static volatile uint32_t s_btn_ignored;   /* presses while a frame was being sent */

typedef struct {
    uint32_t t_ms;
    int16_t  temp_c100;
    uint8_t  state;
    uint8_t  source;
    uint8_t  test;          /* 1 = test record (QFLOOD): counted, not written */
} journal_req_t;

static volatile uint32_t s_journal_written, s_journal_failed, s_journal_test;

/* ---- hooks for rtos_metrics.c (weak: the product works without it) ------- */

__attribute__((weak)) void AppRtos_MetricsInit(void) { }
__attribute__((weak)) void AppRtos_MetricsTick(void) { }

/* ---- accessors for rtos_metrics.c ---------------------------------------- */

void *AppRtos_FrameTaskHandle(void) { return h_frame; }

bool AppRtos_JournalPostTest(void)
{
    journal_req_t r = { .test = 1 };
    return xQueueSend(s_journal_q, &r, 0) == pdTRUE;
}

uint32_t AppRtos_JournalQueueLen(void) { return JOURNAL_QUEUE_LEN; }

void AppRtos_JournalStats(uint32_t *written, uint32_t *failed, uint32_t *test,
                          uint32_t *btn_ignored)
{
    *written = s_journal_written;
    *failed = s_journal_failed;
    *test = s_journal_test;
    *btn_ignored = s_btn_ignored;
}

/* ---- app.c hooks (override the weak defaults) ---------------------------- */

void App_StateLock(void)
{
    if (Platform_RtosRunning()) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    }
}

void App_StateUnlock(void)
{
    if (Platform_RtosRunning()) {
        xSemaphoreGive(s_state_mutex);
    }
}

/* FR-07: the flash write (up to 400 ms when a sector is erased) moves out of
 * the deciding task into the journal task.  Returns false if the queue is
 * full, which damper_changed() reports as "ERR journal write". */
bool App_JournalWrite(uint8_t state, uint8_t source, int16_t temp_c100, uint32_t t_ms)
{
    if (!Platform_RtosRunning()) {
        return Journal_Append(state, source, temp_c100, t_ms);
    }
    journal_req_t r = { .t_ms = t_ms, .temp_c100 = temp_c100,
                        .state = state, .source = source, .test = 0 };
    return xQueueSend(s_journal_q, &r, 0) == pdTRUE;
}

/* ---- interrupts ------------------------------------------------------------ */

void AppRtos_ButtonFromISR(void)
{
    BaseType_t woken = pdFALSE;
    if (h_btn != NULL) {
        vTaskNotifyGiveFromISR(h_btn, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

static void console_rx_from_isr(void)
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(h_cmd, &woken);   /* one count per received byte */
    portYIELD_FROM_ISR(woken);
}

/* ---- tasks ------------------------------------------------------------------ */

/* Reaction to B1: the highest product priority, so the latency no longer
 * depends on what the other tasks are doing (LR3 limitation 2). */
static void task_btn(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        Process_Button_Event();
        if (s_frame_busy) {
            s_btn_ignored++;               /* as in LR3: no second frame queued */
        } else {
            xTaskNotifyGive(h_frame);
        }
    }
}

/* Long operation: capture + stream a full frame.  Lowest product priority, so
 * every other task preempts it (LR3 limitation 1).  Interrupts are still
 * disabled while each strip is read (LR3 limitation 4 is not an OS issue). */
static void task_frame(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        s_frame_busy = true;
        Console_CaptureFrame();
        s_frame_busy = false;
    }
}

static void task_temp(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(TEMP_PERIOD_MS));
        App_TempRun();
    }
}

static void task_tick(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(TICK_PERIOD_MS));
        Platform_ConsoleLock();            /* TICK + temperature stay together */
        App_TickRun();
        Platform_ConsoleUnlock();
        AppRtos_MetricsTick();             /* LOAD line (rtos_metrics.c) */
    }
}

static void task_cmd(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);   /* one byte per count */
        Console_Poll();
    }
}

static void task_camera(void *arg)
{
    (void)arg;
    if (!App_CameraOk()) {
        vTaskSuspend(NULL);                /* as in LR3: no camera, no task */
    }
    for (;;) {
        App_CameraRun();
        vTaskDelay(pdMS_TO_TICKS(CAMERA_PAUSE_MS));
    }
}

static void task_journal(void *arg)
{
    (void)arg;
    journal_req_t r;
    for (;;) {
        if (xQueueReceive(s_journal_q, &r, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (r.test) {
            s_journal_test++;
            continue;
        }
        if (Journal_Append(r.state, r.source, r.temp_c100, r.t_ms)) {
            s_journal_written++;
        } else {
            s_journal_failed++;
            printf("ERR journal write\r\n");
        }
    }
}

static void led_timer_cb(TimerHandle_t t)
{
    (void)t;
    App_LedRun();
}

/* ---- start ------------------------------------------------------------------- */

static void create(TaskFunction_t fn, const char *name, uint32_t words,
                   UBaseType_t prio, TaskHandle_t *h)
{
    BaseType_t ok = xTaskCreate(fn, name, words, NULL, prio, h);
    configASSERT(ok == pdPASS);
}

void AppRtos_Start(void)
{
    Platform_RtosInit();

    s_state_mutex = xSemaphoreCreateMutex();
    s_journal_q   = xQueueCreate(JOURNAL_QUEUE_LEN, sizeof(journal_req_t));
    configASSERT(s_state_mutex && s_journal_q);

    create(task_btn,     "btn",     STACK_BTN,     PRIO_BTN,     &h_btn);
    create(task_temp,    "temp",    STACK_TEMP,    PRIO_TEMP,    &h_temp);
    create(task_tick,    "tick",    STACK_TICK,    PRIO_TICK,    &h_tick);
    create(task_cmd,     "cmd",     STACK_CMD,     PRIO_CMD,     &h_cmd);
    create(task_camera,  "camera",  STACK_CAMERA,  PRIO_CAMERA,  &h_camera);
    create(task_journal, "journal", STACK_JOURNAL, PRIO_JOURNAL, &h_journal);
    create(task_frame,   "frame",   STACK_FRAME,   PRIO_FRAME,   &h_frame);

    s_led_timer = xTimerCreate("led", pdMS_TO_TICKS(LED_PERIOD_MS), pdTRUE, NULL, led_timer_cb);
    configASSERT(s_led_timer != NULL);
    xTimerStart(s_led_timer, 0);

    AppRtos_MetricsInit();                 /* measurement fixtures, trace pins */
    Platform_Console_SetRxNotify(console_rx_from_isr);

    printf("RTOS FreeRTOS %s tick=%u Hz heap=%u free_after_create=%u\r\n",
           tskKERNEL_VERSION_NUMBER, (unsigned)configTICK_RATE_HZ,
           (unsigned)configTOTAL_HEAP_SIZE, (unsigned)xPortGetFreeHeapSize());

    vTaskStartScheduler();
    for (;;) { }                           /* only if the idle task could not be created */
}
