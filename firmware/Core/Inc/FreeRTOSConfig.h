/**
 * @file  FreeRTOSConfig.h
 * @brief Kernel configuration for LR4 (FreeRTOS-Kernel V11.1.0, port GCC/ARM_CM4F).
 *
 * Every value that differs from the FreeRTOS defaults is explained next to
 * it; docs/rtos-design.md, section "Конфігурація ядра", repeats the reasons.
 *
 * Values in #ifndef blocks can be overridden at build time for the LR4
 * scenarios (section 7.2), e.g.
 *   make build EXTRA_CFLAGS="-DconfigTICK_RATE_HZ=100"
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* LAB4_METRICS=1 (set by the firmware Makefile together with rtos_metrics.c
 * and platform_trace.c) turns on run-time stats, the tick hook and the
 * context-switch trace.  Without it the product runs on the kernel alone. */
#ifndef LAB4_METRICS
#define LAB4_METRICS 0
#endif

#if !defined(__ASSEMBLER__)
#include <stdint.h>
extern uint32_t SystemCoreClock;
#if LAB4_METRICS
/* Run-time stats counter: DWT CYCCNT extended to 64 bits (platform_trace.c). */
void     Platform_RtosStatsTimerInit(void);
uint64_t Platform_RtosStatsCounter(void);
/* Context-switch trace and service pins (platform_trace.c). */
void     Platform_TraceSwitchedIn(void *tcb);
#endif
#endif

/* ---- scheduler ---------------------------------------------------------- */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1   /* tasks of equal priority share the CPU */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1   /* CLZ instruction on Cortex-M4 */
#define configUSE_TICKLESS_IDLE                 0   /* keep the tick: LR3 comparison needs a steady 1 ms time base */
#define configCPU_CLOCK_HZ                      (SystemCoreClock)
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ                      1000 /* 1 ms, same resolution as HAL_GetTick in LR3 */
#endif
#define configMAX_PRIORITIES                    8   /* 0 idle .. 7; product uses 1..6 */
#define configMINIMAL_STACK_SIZE                128 /* words; idle task only */
#define configMAX_TASK_NAME_LEN                 12
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1   /* console and bus locks may nest */
#define configUSE_COUNTING_SEMAPHORES           0
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_QUEUE_SETS                    0
#define configUSE_NEWLIB_REENTRANT              0   /* printf is serialised by a mutex instead,
                                                     * see platform_board.c; saves a struct
                                                     * _reent per task */
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0
#define configUSE_MINI_LIST_ITEM                1
#define configSTACK_DEPTH_TYPE                  uint32_t
#define configMESSAGE_BUFFER_LENGTH_TYPE        size_t
#define configHEAP_CLEAR_MEMORY_ON_FREE         0
#define configUSE_APPLICATION_TASK_TAG          0

/* ---- memory ------------------------------------------------------------- */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#ifndef configTOTAL_HEAP_SIZE
#define configTOTAL_HEAP_SIZE                   (24 * 1024) /* heap_4; stacks + TCBs + queues,
                                                             * sized from the task table plus
                                                             * a margin, then checked with HEAP */
#endif
#define configAPPLICATION_ALLOCATED_HEAP        0
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP 0

/* ---- hooks -------------------------------------------------------------- */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     LAB4_METRICS /* service pin PC11 + DWT extension */
#ifndef configCHECK_FOR_STACK_OVERFLOW
#define configCHECK_FOR_STACK_OVERFLOW          2   /* pattern check at every switch */
#endif
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0

/* ---- statistics --------------------------------------------------------- */
#define configUSE_TRACE_FACILITY                1
#if LAB4_METRICS
#define configGENERATE_RUN_TIME_STATS           1
#define configRUN_TIME_COUNTER_TYPE             uint64_t /* 32-bit CYCCNT wraps every 51 s */
#define configUSE_STATS_FORMATTING_FUNCTIONS    1
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() Platform_RtosStatsTimerInit()
#define portGET_RUN_TIME_COUNTER_VALUE()        Platform_RtosStatsCounter()
#else
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#endif

/* ---- co-routines, timers ------------------------------------------------- */
#define configUSE_CO_ROUTINES                   0
#define configUSE_TIMERS                        1   /* LED blink timer (FR-08) */
#define configTIMER_TASK_PRIORITY               2
#define configTIMER_QUEUE_LENGTH                8
#define configTIMER_TASK_STACK_DEPTH            256

/* ---- interrupt priorities (4 priority bits on STM32F4) ------------------ */
#define configPRIO_BITS                         4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
/* ISRs that call FromISR API (USART2, EXTI15_10) must have priority >= 5.
 * TIM5 (HAL time base) runs at 0 and never calls the kernel. */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_API_CALL_INTERRUPT_PRIORITY   configMAX_SYSCALL_INTERRUPT_PRIORITY

/* ---- optional API ------------------------------------------------------- */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          0

/* ---- assert ------------------------------------------------------------- */
#if !defined(__ASSEMBLER__)
void Platform_RtosAssertFailed(const char *file, int line);
#endif
#define configASSERT(x) do { if (!(x)) Platform_RtosAssertFailed(__FILE__, __LINE__); } while (0)

/* ---- trace: every context switch -------------------------------------- */
#if LAB4_METRICS
#define traceTASK_SWITCHED_IN() Platform_TraceSwitchedIn((void *)pxCurrentTCB)
#endif

/* ---- map the port handlers onto the CMSIS vector names ------------------ */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
