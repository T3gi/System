/**
 * @file  platform_rtos.c
 * @brief FreeRTOS glue on the hardware side (LR4): bus locks and kernel
 *        hooks.  See platform_rtos.h.  Measurement services (trace, run-time
 *        counter, service pins) are in platform_trace.c.
 */
#include "platform_rtos.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <string.h>

extern UART_HandleTypeDef huart2;

static SemaphoreHandle_t s_bus[PLATFORM_BUS_COUNT];

/* ---- locks -------------------------------------------------------------- */

void Platform_RtosInit(void)
{
    for (int i = 0; i < PLATFORM_BUS_COUNT; i++) {
        s_bus[i] = xSemaphoreCreateRecursiveMutex();
        configASSERT(s_bus[i] != NULL);
    }
}

bool Platform_RtosRunning(void)
{
    return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

void Platform_BusLock(platform_bus_t bus)
{
    if (Platform_RtosRunning() && s_bus[bus] != NULL) {
        xSemaphoreTakeRecursive(s_bus[bus], portMAX_DELAY);
    }
}

void Platform_BusUnlock(platform_bus_t bus)
{
    if (Platform_RtosRunning() && s_bus[bus] != NULL) {
        xSemaphoreGiveRecursive(s_bus[bus]);
    }
}

void Platform_ConsoleWriteRaw(const char *s)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)s, (uint16_t)strlen(s), 100);
}

/* ---- kernel hooks --------------------------------------------------------- */

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    taskDISABLE_INTERRUPTS();
    Platform_ConsoleWriteRaw("\r\nFATAL stack overflow in ");
    Platform_ConsoleWriteRaw(name);
    Platform_ConsoleWriteRaw("\r\n");
    for (;;) { }
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    Platform_ConsoleWriteRaw("\r\nFATAL FreeRTOS heap exhausted (pvPortMalloc returned NULL)\r\n");
    for (;;) { }
}

void Platform_RtosAssertFailed(const char *file, int line)
{
    char num[12];
    int i = (int)sizeof(num) - 1;
    num[i] = '\0';
    unsigned v = (unsigned)line;
    do { num[--i] = (char)('0' + v % 10); v /= 10; } while (v != 0 && i > 0);

    taskDISABLE_INTERRUPTS();
    Platform_ConsoleWriteRaw("\r\nFATAL configASSERT ");
    Platform_ConsoleWriteRaw(file);
    Platform_ConsoleWriteRaw(":");
    Platform_ConsoleWriteRaw(&num[i]);
    Platform_ConsoleWriteRaw("\r\n");
    for (;;) { }
}
