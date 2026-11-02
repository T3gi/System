#ifndef INC_MEM_DIAG_H_
#define INC_MEM_DIAG_H_

#include <stdint.h>
#include <stdio.h>
#include <malloc.h>
#include "stm32f4xx.h"

extern uint32_t _estack;
extern uint32_t _Min_Stack_Size;

#define STACK_PATTERN 0xDEADBEEFu

static inline void stack_paint(void)
{
    uint32_t *p = (uint32_t *)((uint32_t)&_estack - (uint32_t)&_Min_Stack_Size);
    uint32_t *sp = (uint32_t *)__get_MSP();
    while (p < sp - 16) {
        *p++ = STACK_PATTERN;
    }
}

static inline uint32_t stack_used_bytes(void)
{
    uint32_t *p = (uint32_t *)((uint32_t)&_estack - (uint32_t)&_Min_Stack_Size);
    while (p < (uint32_t *)&_estack && *p == STACK_PATTERN) {
        p++;
    }
    return (uint32_t)&_estack - (uint32_t)p;
}

static inline void heap_report(void)
{
    struct mallinfo mi = mallinfo();
    printf("HEAP arena=%u used=%u free=%u\r\n",
           (unsigned)mi.arena, (unsigned)mi.uordblks, (unsigned)mi.fordblks);
}

static inline void stack_report(void)
{
    uint32_t used = stack_used_bytes();
    uint32_t total = (uint32_t)&_Min_Stack_Size;
    printf("STACK reserved=%lu used_peak=%lu free_margin=%lu bytes\r\n",
           (unsigned long)total, (unsigned long)used, (unsigned long)(total - used));
}

#endif /* INC_MEM_DIAG_H_ */