/**
 * @file  platform_board.h
 * @brief Nucleo-F411RE board services: USART2 console, B1 button, LD2 LED,
 *        reset.  printf() is retargeted to USART2 in platform_board.c.
 */
#ifndef PLATFORM_BOARD_H
#define PLATFORM_BOARD_H

#include <stdbool.h>
#include <stdint.h>

/** Next received console byte, or -1 if none (non-blocking). */
int  Platform_Console_GetChar(void);
/** Received bytes / overrun errors since boot (diagnostics for PING). */
uint32_t Platform_Console_RxBytes(void);
uint32_t Platform_Console_RxOverruns(void);

/** true while the user button B1 is held down. */
bool Platform_ButtonPressed(void);
void Platform_LedToggle(void);

/** Wait for the console to finish sending, then reboot the MCU. */
void Platform_SystemReset(void);

#endif /* PLATFORM_BOARD_H */
