/**
 * @file  platform_trace.h
 * @brief LR4 measurement services: context-switch trace, 64-bit cycle counter
 *        for run-time stats, service pins, interrupt-stack (MSP) usage.
 *
 * Service pins (Saleae):
 *   PC8  high while any task except IDLE runs  -> CPU busy (LR3: App_Poll pass)
 *   PC9  high from the B1 interrupt until the button task runs (as in LR3)
 *   PC10 high while the long-operation task (frame capture) runs -> preemption
 *   PC11 pulse in every tick interrupt                          -> tick cost
 */
#ifndef PLATFORM_TRACE_H
#define PLATFORM_TRACE_H

#include <stdbool.h>
#include <stdint.h>

/** Configure PC10 / PC11; call once before the scheduler starts. */
void Platform_TraceInit(void);

/** One entry of the context-switch trace. */
typedef struct {
    uint32_t cycles;        /* DWT CYCCNT at the switch */
    void    *task;          /* TaskHandle_t of the task switched in */
} platform_trace_t;

/** Stop / restart recording (the dump itself must not be recorded). */
void Platform_TraceFreeze(bool frozen);

/** Copy up to max entries, oldest first; returns the number copied. */
uint32_t Platform_TraceRead(platform_trace_t *out, uint32_t max);

/** The task whose run time the long-operation pin PC10 shows. */
void Platform_TraceSetLongOpTask(void *task);

/** Extended 64-bit DWT counter, also used for run-time stats. */
uint64_t Platform_RtosStatsCounter(void);

/** Cycles of the counter per microsecond. */
uint32_t Platform_RtosCyclesPerUs(void);

/** Interrupt stack (MSP): reserved size and peak use, from the 0xDEADBEEF
 *  pattern painted at reset (mem_diag.h). */
uint32_t Platform_MspReservedBytes(void);
uint32_t Platform_MspUsedBytes(void);

#endif /* PLATFORM_TRACE_H */
