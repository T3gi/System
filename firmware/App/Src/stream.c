/**
 * @file  stream.c
 * @brief CRC-32 and base64 streaming, moved from main.c unchanged.
 */
#include "stream.h"
#include <stddef.h>
#include <stdio.h>

uint32_t Stream_Crc32Update(uint32_t crc, const uint8_t *p, uint32_t n)
{
    crc = ~crc;
    for (uint32_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

void Stream_SendB64(const char *marker, const uint8_t *src, int size,
                    uint32_t width, uint32_t start_line, uint32_t lines,
                    const ov7670_capture_stats_t *st)
{
    static const char alpha[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    printf("---%s-BEGIN w=%lu start=%lu lines=%lu bytes=%d crc=%08lX "
           "got=%lu drop=%lu linemin=%lu linemax=%lu short=%lu---\r\n",
           marker, (unsigned long)width, (unsigned long)start_line,
           (unsigned long)lines, size,
           (unsigned long)Stream_Crc32Update(0, src, (uint32_t)size),
           (unsigned long)st->lines, (unsigned long)st->dropouts,
           (unsigned long)st->line_min, (unsigned long)st->line_max,
           (unsigned long)st->short_lines);

    char line[80];
    int  col = 0;
    const uint8_t *p   = src;
    const uint8_t *end = src + size;

    while (p + 3 <= end) {
        uint32_t v = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        line[col++] = alpha[(v >> 18) & 0x3F];
        line[col++] = alpha[(v >> 12) & 0x3F];
        line[col++] = alpha[(v >>  6) & 0x3F];
        line[col++] = alpha[(v      ) & 0x3F];
        p += 3;
        if (col >= 76) { line[col] = '\0'; printf("%s\r\n", line); col = 0; }
    }

    size_t rem = (size_t)(end - p);
    if (rem == 1) {
        uint32_t v = ((uint32_t)p[0] << 16);
        line[col++] = alpha[(v >> 18) & 0x3F];
        line[col++] = alpha[(v >> 12) & 0x3F];
        line[col++] = '='; line[col++] = '=';
    } else if (rem == 2) {
        uint32_t v = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8);
        line[col++] = alpha[(v >> 18) & 0x3F];
        line[col++] = alpha[(v >> 12) & 0x3F];
        line[col++] = alpha[(v >>  6) & 0x3F];
        line[col++] = '=';
    }
    if (col > 0) { line[col] = '\0'; printf("%s\r\n", line); }

    printf("---%s-END---\r\n", marker);
}
