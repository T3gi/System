/**
 * @file  stream.h
 * @brief Binary data to the host over the UART console: CRC-32 and base64
 *        chunks between machine-parsable markers.  No hardware access.
 */
#ifndef STREAM_H
#define STREAM_H

#include <stdint.h>
#include "ov7670_capture.h"

/**
 * CRC-32 (IEEE 802.3, the same polynomial Python's zlib.crc32 uses) so the
 * host can verify every strip and re-request corrupted ones instead of
 * silently assembling a torn image.  Start with crc = 0.
 */
uint32_t Stream_Crc32Update(uint32_t crc, const uint8_t *p, uint32_t n);

/**
 * Print `size` bytes of src as base64 between
 * "---<marker>-BEGIN w=.. start=.. lines=.. bytes=.. crc=.. got=.. ...---" and
 * "---<marker>-END---".  The capture telemetry goes into the BEGIN line.
 */
void Stream_SendB64(const char *marker, const uint8_t *src, int size,
                    uint32_t width, uint32_t start_line, uint32_t lines,
                    const ov7670_capture_stats_t *st);

#endif /* STREAM_H */
