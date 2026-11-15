/**
 * @file  platform_rtos.h
 * @brief Hardware services the FreeRTOS build needs (LR4): bus locks, raw
 *        console output for kernel hooks, the hooks themselves.
 *
 * Before the scheduler starts every lock is a no-op, so the boot self-test
 * and the host tests run exactly as in LR3.
 */
#ifndef PLATFORM_RTOS_H
#define PLATFORM_RTOS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PLATFORM_BUS_I2C = 0,   /* I2C1: MPU6050, GY-89 and the camera SCCB */
    PLATFORM_BUS_SPI,       /* SPI2: W25Q64 */
    PLATFORM_BUS_CONSOLE,   /* USART2 TX: one printf() at a time */
    PLATFORM_BUS_COUNT
} platform_bus_t;

/** Create the bus mutexes; call once before vTaskStartScheduler(). */
void Platform_RtosInit(void);

/** True once the scheduler runs (locks and vTaskDelay are active). */
bool Platform_RtosRunning(void);

/** Recursive, priority-inheriting lock of one bus; no-op before the scheduler. */
void Platform_BusLock(platform_bus_t bus);
void Platform_BusUnlock(platform_bus_t bus);

/** Write a string straight to USART2, bypassing locks (hooks, fatal errors). */
void Platform_ConsoleWriteRaw(const char *s);

#endif /* PLATFORM_RTOS_H */
