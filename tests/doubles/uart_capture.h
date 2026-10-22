/* Captures what the drivers print.  On the board printf() goes to USART2;
 * here the test build links with -Wl,--wrap=printf so every printf() call in
 * firmware code lands in a buffer the test can compare with the UART log. */
#ifndef UART_CAPTURE_H
#define UART_CAPTURE_H

void uart_reset(void);
const char *uart_out(void);

#endif
