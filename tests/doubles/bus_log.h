/* Text log of every bus access made through the test doubles.
 * Entries are short and ';'-terminated, e.g. "I2C R D0 41 2;" or "SPI 06;",
 * so a whole transaction sequence can be compared with one string. */
#ifndef BUS_LOG_H
#define BUS_LOG_H

void bus_log_reset(void);
void bus_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
const char *bus_log_str(void);
/* Number of non-overlapping occurrences of needle in the log. */
int bus_log_count(const char *needle);

#endif
