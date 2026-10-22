#include "uart_capture.h"
#include <stdarg.h>
#include <stdio.h>

static char   s_out[1u << 16];
static size_t s_len;

int __wrap_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

int __wrap_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(s_out + s_len, sizeof s_out - s_len, fmt, ap);
    va_end(ap);
    if (n > 0 && s_len + (size_t)n < sizeof s_out) s_len += (size_t)n;
    return n;
}

void uart_reset(void)
{
    s_len = 0;
    s_out[0] = '\0';
}

const char *uart_out(void) { return s_out; }
