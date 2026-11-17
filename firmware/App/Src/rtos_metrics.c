/**
 * @file  rtos_metrics.c
 * @brief LR4 measurement layer: OS metrics commands (manual section 6) and the
 *        OS-specific scenarios (section 7.2).
 *
 * Attaches to the product through the weak hooks of app_rtos.c and console.c;
 * the LR3 commands and log format are unchanged.  Built only with
 * -DLAB4_METRICS=1 (firmware Makefile).
 *
 *   LOAD pct=     every second: 100 % minus the IDLE share of that second
 *   TASKS         run-time stats + task list                         (6.2)
 *   STACK         lowest free stack of every task + MSP               (6.3)
 *   HEAP          FreeRTOS heap: free, minimum ever, total            (6.4)
 *   TRACE [n]     last n context switches with DWT time               (6.5)
 *   CSWITCH [n]   context-switch cost                                 (6.5)
 *   STARVE <ms>   a prio-5 task that never blocks                     (7.2-3)
 *   INVERT M|S    priority inversion, mutex vs binary semaphore       (7.2-1)
 *   QFLOOD [n]    journal queue overflow                              (7.2-4)
 *   JSTAT         journal counters
 *
 * Fixture tasks are created at start and stay blocked until their command:
 *   csw 7, hog 5, inv_hi 5, inv_med 4, inv_lo 1.
 */
#include "app_rtos.h"
#include "console.h"
#include "platform_trace.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRIO_CSW      7
#define PRIO_HOG      5
#define PRIO_INV_HI   5
#define PRIO_INV_MED  4
#define PRIO_INV_LO   1
#define STACK_FIXTURE 192

static TaskHandle_t h_csw, h_hog, h_inv_hi, h_inv_med, h_inv_lo;

/* ---- LOAD ------------------------------------------------------------------ */

/* LOAD now counts real work: 100 % minus the IDLE share of the last window.
 * (LR3 counted the whole App_Poll pass, see LR3 report 3.1.) */
void AppRtos_MetricsTick(void)
{
    static uint64_t prev_total, prev_idle;
    uint64_t total = Platform_RtosStatsCounter();
    uint64_t idle  = ulTaskGetIdleRunTimeCounter();
    uint64_t dt = total - prev_total;
    uint64_t di = idle - prev_idle;
    prev_total = total;
    prev_idle  = idle;
    if (dt == 0) {
        return;
    }
    unsigned busy = (unsigned)(100u - (unsigned)((di * 100u) / dt));
    printf("LOAD pct=%u%%\r\n", busy);
}

/* ---- fixtures ---------------------------------------------------------------- */

static volatile uint32_t s_csw_t0, s_csw_dt;

/* CSWITCH: cycles from xTaskNotifyGive() in cmd (prio 3) to the first
 * instruction after ulTaskNotifyTake() here (prio 7). */
static void task_csw(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        s_csw_dt = (uint32_t)Platform_RtosStatsCounter() - s_csw_t0;
    }
}

static void busy_wait_ms(uint32_t ms)
{
    uint64_t end = Platform_RtosStatsCounter()
                 + (uint64_t)ms * 1000u * Platform_RtosCyclesPerUs();
    while (Platform_RtosStatsCounter() < end) { }
}

static volatile uint32_t s_hog_ms;

/* STARVE: a high-priority task that never blocks. */
static void task_hog(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        busy_wait_ms(s_hog_ms);
        printf("STARVE done busy_ms=%lu\r\n", (unsigned long)s_hog_ms);
    }
}

/* INVERT: lo takes the lock and works HOLD ms; hi wants the lock; med burns
 * MED ms of CPU.  With a mutex lo inherits hi's priority and med cannot
 * preempt it; with a binary semaphore med runs first and hi waits longer. */
#define INV_HOLD_MS  20
#define INV_MED_MS  100
static SemaphoreHandle_t s_inv_mutex, s_inv_sem;
static volatile bool     s_inv_use_mutex;

static SemaphoreHandle_t inv_lock(void)
{
    return s_inv_use_mutex ? s_inv_mutex : s_inv_sem;
}

static void task_inv_lo(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(inv_lock(), portMAX_DELAY);
        xTaskNotifyGive(h_inv_hi);         /* hi runs now and blocks on the lock */
        xTaskNotifyGive(h_inv_med);        /* med preempts lo unless lo inherited */
        busy_wait_ms(INV_HOLD_MS);
        xSemaphoreGive(inv_lock());
    }
}

static void task_inv_med(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        busy_wait_ms(INV_MED_MS);
    }
}

static void task_inv_hi(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint64_t t_req = Platform_RtosStatsCounter();
        xSemaphoreTake(inv_lock(), portMAX_DELAY);
        uint64_t waited = Platform_RtosStatsCounter() - t_req;
        xSemaphoreGive(inv_lock());
        printf("INV lock=%s hi_wait_us=%lu hold_ms=%u med_ms=%u\r\n",
               s_inv_use_mutex ? "mutex" : "binsem",
               (unsigned long)(waited / Platform_RtosCyclesPerUs()),
               INV_HOLD_MS, INV_MED_MS);
    }
}

/* ---- commands ------------------------------------------------------------------ */

void Console_ExtraHelp(void)
{
    printf("  -- FreeRTOS (LR4) --\r\n");
    printf("  TASKS                      run-time stats + task list\r\n");
    printf("  STACK                      free stack per task (words) + MSP\r\n");
    printf("  HEAP                       FreeRTOS heap free / min ever / total\r\n");
    printf("  TRACE [n]                  last n context switches (DWT time)\r\n");
    printf("  CSWITCH [n]                context-switch cost, n samples\r\n");
    printf("  STARVE <ms>                prio-5 task busy-waits ms (starvation)\r\n");
    printf("  INVERT M|S                 priority inversion with mutex / binary semaphore\r\n");
    printf("  QFLOOD [n]                 n journal records without waiting (queue full)\r\n");
    printf("  JSTAT                      journal counters\r\n");
}

static void cmd_tasks(void)
{
    static char buf[768];
    vTaskGetRunTimeStats(buf);
    printf("TASKS runtime (name abs_cycles pct)\r\n%s", buf);
    vTaskList(buf);
    printf("TASKS list (name state prio free_words num)\r\n%s", buf);
}

static void cmd_stack(void)
{
    static TaskStatus_t st[16];
    UBaseType_t n = uxTaskGetSystemState(st, 16, NULL);
    for (UBaseType_t i = 0; i < n; i++) {
        printf("STACK task=%s prio=%u free_min_words=%u free_min_bytes=%u\r\n",
               st[i].pcTaskName, (unsigned)st[i].uxCurrentPriority,
               (unsigned)st[i].usStackHighWaterMark,
               (unsigned)st[i].usStackHighWaterMark * 4u);
    }
    printf("STACK task=MSP reserved=%lu used_peak=%lu\r\n",
           (unsigned long)Platform_MspReservedBytes(),
           (unsigned long)Platform_MspUsedBytes());
}

static void cmd_heap(void)
{
    printf("HEAP free=%u min_ever_free=%u total=%u used_peak=%u\r\n",
           (unsigned)xPortGetFreeHeapSize(),
           (unsigned)xPortGetMinimumEverFreeHeapSize(),
           (unsigned)configTOTAL_HEAP_SIZE,
           (unsigned)(configTOTAL_HEAP_SIZE - xPortGetMinimumEverFreeHeapSize()));
}

static void cmd_trace(uint32_t n)
{
    static platform_trace_t t[128];
    if (n == 0 || n > 128) {
        n = 64;
    }
    Platform_TraceFreeze(true);
    uint32_t got = Platform_TraceRead(t, n);
    uint32_t cpu = Platform_RtosCyclesPerUs();
    for (uint32_t i = 0; i < got; i++) {
        uint32_t dt = (i == 0) ? 0 : (t[i].cycles - t[i - 1].cycles) / cpu;
        printf("TRC t_us=%lu dt_us=%lu task=%s\r\n",
               (unsigned long)(t[i].cycles / cpu), (unsigned long)dt,
               pcTaskGetName((TaskHandle_t)t[i].task));
    }
    Platform_TraceFreeze(false);
}

static void cmd_cswitch(uint32_t n)
{
    if (n == 0 || n > 10000) {
        n = 100;
    }
    uint32_t mn = UINT32_MAX, mx = 0;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        s_csw_t0 = (uint32_t)Platform_RtosStatsCounter();
        xTaskNotifyGive(h_csw);            /* csw has a higher priority: runs now */
        uint32_t d = s_csw_dt;
        if (d < mn) mn = d;
        if (d > mx) mx = d;
        sum += d;
    }
    uint32_t cpu = Platform_RtosCyclesPerUs();
    printf("CSWITCH n=%lu min_cyc=%lu avg_cyc=%lu max_cyc=%lu min_ns=%lu avg_ns=%lu max_ns=%lu\r\n",
           (unsigned long)n, (unsigned long)mn, (unsigned long)(sum / n), (unsigned long)mx,
           (unsigned long)(mn * 1000u / cpu), (unsigned long)((sum / n) * 1000u / cpu),
           (unsigned long)(mx * 1000u / cpu));
}

static void cmd_qflood(uint32_t n)
{
    if (n == 0 || n > 1000) {
        n = 32;
    }
    uint32_t ok = 0, dropped = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (AppRtos_JournalPostTest()) ok++;
        else dropped++;
    }
    printf("QFLOOD sent=%lu queued=%lu dropped=%lu queue_len=%lu\r\n",
           (unsigned long)n, (unsigned long)ok, (unsigned long)dropped,
           (unsigned long)AppRtos_JournalQueueLen());
}

bool Console_ExtraCommand(int argc, char **argv)
{
    uint32_t a1 = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 0) : 0;

    if (!strcmp(argv[0], "TASKS")) {
        cmd_tasks();
    } else if (!strcmp(argv[0], "STACK")) {
        cmd_stack();
    } else if (!strcmp(argv[0], "HEAP")) {
        cmd_heap();
    } else if (!strcmp(argv[0], "TRACE")) {
        cmd_trace(a1);
    } else if (!strcmp(argv[0], "CSWITCH")) {
        cmd_cswitch(a1);
    } else if (!strcmp(argv[0], "STARVE")) {
        s_hog_ms = a1 ? a1 : 2000;
        xTaskNotifyGive(h_hog);
    } else if (!strcmp(argv[0], "INVERT")) {
        s_inv_use_mutex = !(argc > 1 && (argv[1][0] == 'S' || argv[1][0] == 's'));
        xTaskNotifyGive(h_inv_lo);
    } else if (!strcmp(argv[0], "QFLOOD")) {
        cmd_qflood(a1);
    } else if (!strcmp(argv[0], "JSTAT")) {
        uint32_t w, f, t, ign;
        AppRtos_JournalStats(&w, &f, &t, &ign);
        printf("JOURNAL written=%lu failed=%lu test=%lu btn_ignored=%lu\r\n",
               (unsigned long)w, (unsigned long)f, (unsigned long)t, (unsigned long)ign);
    } else {
        return false;
    }
    return true;
}

/* ---- start ---------------------------------------------------------------------- */

static void create(TaskFunction_t fn, const char *name, UBaseType_t prio, TaskHandle_t *h)
{
    BaseType_t ok = xTaskCreate(fn, name, STACK_FIXTURE, NULL, prio, h);
    configASSERT(ok == pdPASS);
}

void AppRtos_MetricsInit(void)
{
    Platform_TraceInit();
    Platform_TraceSetLongOpTask(AppRtos_FrameTaskHandle());

    s_inv_mutex = xSemaphoreCreateMutex();
    s_inv_sem   = xSemaphoreCreateBinary();
    configASSERT(s_inv_mutex && s_inv_sem);
    xSemaphoreGive(s_inv_sem);

    create(task_csw,     "csw",     PRIO_CSW,     &h_csw);
    create(task_hog,     "hog",     PRIO_HOG,     &h_hog);
    create(task_inv_hi,  "inv_hi",  PRIO_INV_HI,  &h_inv_hi);
    create(task_inv_med, "inv_med", PRIO_INV_MED, &h_inv_med);
    create(task_inv_lo,  "inv_lo",  PRIO_INV_LO,  &h_inv_lo);
}
