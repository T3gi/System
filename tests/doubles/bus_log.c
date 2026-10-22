#include "bus_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char   s_buf[1u << 17];
static size_t s_len;

void bus_log_reset(void)
{
    s_len = 0;
    s_buf[0] = '\0';
}

void bus_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(s_buf + s_len, sizeof s_buf - s_len, fmt, ap);
    va_end(ap);
    if (n > 0 && s_len + (size_t)n < sizeof s_buf) s_len += (size_t)n;
}

const char *bus_log_str(void) { return s_buf; }

int bus_log_count(const char *needle)
{
    int n = 0;
    size_t step = strlen(needle);
    for (const char *p = s_buf; (p = strstr(p, needle)) != NULL; p += step) n++;
    return n;
}
