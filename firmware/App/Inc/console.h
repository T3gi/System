/**
 * @file  console.h
 * @brief UART command interpreter.
 *
 * Everything the host automation needs is driven from here, so a full
 * experiment sweep (mode change, geometry probe, register poke, strip
 * capture) never requires a reflash.  Each command answers with exactly one
 * terminating "OK" or "ERR ..." line, which is what the host waits on.
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

/** Print the command list. */
void Console_Help(void);

/** Read pending console bytes; run a command when a line is complete. */
void Console_Poll(void);

/** Execute one command line (modified in place). */
void Console_Handle(char *cmd);

/** Capture a full frame of the current mode and stream it (B1 button). */
void Console_CaptureFrame(void);

/** Commands executed since boot (shown by PING and the heartbeat). */
uint32_t Console_CommandCount(void);

#endif /* CONSOLE_H */
