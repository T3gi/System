#ifndef INC_PERF_H_
#define INC_PERF_H_

#include "stm32f4xx.h"
#include <stdint.h>
#include <stdio.h>

typedef struct {
    const char *name;
    uint32_t count;
    uint32_t min;
    uint32_t max;
    uint64_t sum;
} perf_stat_t;

static inline void perf_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline uint32_t perf_now(void)
{
    return DWT->CYCCNT;
}

static inline uint32_t perf_cycles_to_us(uint32_t cyc)
{
    return cyc / (SystemCoreClock / 1000000u);
}

static inline void perf_add(perf_stat_t *s, uint32_t cyc)
{
    if (s->count == 0 || cyc < s->min) s->min = cyc;
    if (cyc > s->max) s->max = cyc;
    s->sum += cyc;
    s->count++;
}

static inline void perf_print_stat(const perf_stat_t *s)
{
    if (s->count == 0) {
        printf("STAT name=%s n=0\r\n", s->name);
        return;
    }
    uint32_t avg_cyc = (uint32_t)(s->sum / s->count);
    printf("STAT name=%s n=%lu min_us=%lu avg_us=%lu max_us=%lu\r\n",
           s->name,
           (unsigned long)s->count,
           (unsigned long)perf_cycles_to_us(s->min),
           (unsigned long)perf_cycles_to_us(avg_cyc),
           (unsigned long)perf_cycles_to_us(s->max));
}

#endif /* INC_PERF_H_ */