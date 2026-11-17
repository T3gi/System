/**
 * @file  platform_trace.c
 * @brief LR4 measurement services on the hardware side.  See platform_trace.h.
 *
 * Built only with -DLAB4_METRICS=1 (firmware Makefile), which also turns on
 * run-time stats, the tick hook and traceTASK_SWITCHED_IN in FreeRTOSConfig.h.
 */
#include "platform_trace.h"
#include "main.h"
#include "mem_diag.h"
#include "FreeRTOS.h"
#include "task.h"

#define PIN_BUSY    GPIO_PIN_8
#define PIN_LONGOP  GPIO_PIN_10
#define PIN_TICK    GPIO_PIN_11

void Platform_TraceInit(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin   = PIN_LONGOP | PIN_TICK;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &g);
    GPIOC->BSRR = (uint32_t)(PIN_LONGOP | PIN_TICK) << 16U;
}

/* ---- 64-bit cycle counter ------------------------------------------------ */

static uint32_t s_cyc_last;
static uint32_t s_cyc_high;

/* portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() */
void Platform_RtosStatsTimerInit(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_cyc_last = DWT->CYCCNT;
}

/* portGET_RUN_TIME_COUNTER_VALUE().  Called from tasks, the tick interrupt
 * and the context switch; the interrupt mask keeps the two words consistent.
 * The tick hook calls it every millisecond, far more often than the 51 s
 * wrap period of CYCCNT at 84 MHz. */
uint64_t Platform_RtosStatsCounter(void)
{
    UBaseType_t m = portSET_INTERRUPT_MASK_FROM_ISR();
    uint32_t now = DWT->CYCCNT;
    if (now < s_cyc_last) {
        s_cyc_high++;
    }
    s_cyc_last = now;
    uint64_t v = ((uint64_t)s_cyc_high << 32) | now;
    portCLEAR_INTERRUPT_MASK_FROM_ISR(m);
    return v;
}

uint32_t Platform_RtosCyclesPerUs(void)
{
    return SystemCoreClock / 1000000u;
}

/* ---- context-switch trace ------------------------------------------------- */

#define TRACE_LEN 256
static platform_trace_t s_trace[TRACE_LEN];
static uint32_t s_trace_head;      /* next slot */
static uint32_t s_trace_count;
static volatile bool s_trace_frozen;
static void *s_longop_task;
static void *s_idle_task;

void Platform_TraceSetLongOpTask(void *task)
{
    s_longop_task = task;
}

/* traceTASK_SWITCHED_IN: runs inside the kernel with interrupts masked. */
void Platform_TraceSwitchedIn(void *tcb)
{
    if (s_idle_task == NULL) {
        s_idle_task = xTaskGetIdleTaskHandle();
    }
    if (tcb == s_idle_task) {
        GPIOC->BSRR = (uint32_t)PIN_BUSY << 16U;
    } else {
        GPIOC->BSRR = PIN_BUSY;
    }
    if (tcb == s_longop_task) {
        GPIOC->BSRR = PIN_LONGOP;
    } else {
        GPIOC->BSRR = (uint32_t)PIN_LONGOP << 16U;
    }
    if (s_trace_frozen) {
        return;
    }
    s_trace[s_trace_head].cycles = DWT->CYCCNT;
    s_trace[s_trace_head].task   = tcb;
    s_trace_head = (s_trace_head + 1) % TRACE_LEN;
    if (s_trace_count < TRACE_LEN) {
        s_trace_count++;
    }
}

void Platform_TraceFreeze(bool frozen)
{
    s_trace_frozen = frozen;
}

uint32_t Platform_TraceRead(platform_trace_t *out, uint32_t max)
{
    uint32_t n = s_trace_count < max ? s_trace_count : max;
    uint32_t start = (s_trace_head + TRACE_LEN - n) % TRACE_LEN;
    for (uint32_t i = 0; i < n; i++) {
        out[i] = s_trace[(start + i) % TRACE_LEN];
    }
    return n;
}

/* ---- tick hook --------------------------------------------------------------- */

void vApplicationTickHook(void)
{
    GPIOC->BSRR = PIN_TICK;
    (void)Platform_RtosStatsCounter();     /* keeps the 64-bit extension current */
    GPIOC->BSRR = (uint32_t)PIN_TICK << 16U;
}

/* ---- interrupt stack ---------------------------------------------------------- */

uint32_t Platform_MspReservedBytes(void)
{
    return (uint32_t)&_Min_Stack_Size;
}

uint32_t Platform_MspUsedBytes(void)
{
    return stack_used_bytes();
}
