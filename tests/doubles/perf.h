/* Host replacement of firmware/Core/Inc/perf.h for the App tests.  The real one
 * needs the STM32 headers (DWT cycle counter); app.c also writes GPIOC->BSRR
 * directly, so that register is modelled here too. */
#ifndef PERF_H_DOUBLE
#define PERF_H_DOUBLE

#include <stdint.h>

typedef struct {
    const char *name;
    uint32_t    count;
} perf_stat_t;

typedef struct {
    volatile uint32_t BSRR;
} fake_gpio_t;

extern fake_gpio_t fake_gpioc;
extern uint32_t    fake_cycles;       /* what perf_now() returns */

#define GPIOC       (&fake_gpioc)
#define GPIO_PIN_9  ((uint16_t)0x0200u)

static inline uint32_t perf_now(void)                 { return fake_cycles; }
static inline uint32_t perf_cycles_to_us(uint32_t c)  { return c / 84u; }
static inline void     perf_add(perf_stat_t *s, uint32_t c) { (void)c; s->count++; }

#endif
