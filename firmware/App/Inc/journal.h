/**
 * @file  journal.h
 * @brief Event journal of FR-07 in the W25Q64: every damper state change with
 *        its source, written as a 16-byte record into a ring of
 *        JOURNAL_SECTORS sectors at the top of the chip (frames saved by
 *        FERASE/FTEST live from address 0, so the two never meet).
 *
 * Record (little-endian):
 *   0..3  seq        increases by one per record, never 0xFFFFFFFF
 *   4..7  t_ms       milliseconds since that boot
 *   8..9  temp_c100  temperature at the moment of the change, 0.01 C
 *   10    state      damper_state_t
 *   11    source     damper_source_t
 *   12..15 crc32     Stream_Crc32Update over bytes 0..11
 *
 * An erased slot reads all 0xFF.  A slot with a bad CRC (power lost during
 * the write) is skipped and counted, never trusted.
 */
#ifndef JOURNAL_H
#define JOURNAL_H

#include <stdbool.h>
#include <stdint.h>

#define JOURNAL_SECTORS      4u
#define JOURNAL_RECORD_SIZE  16u

typedef struct {
    uint32_t seq;
    uint32_t t_ms;
    int16_t  temp_c100;
    uint8_t  state;
    uint8_t  source;
} journal_entry_t;

/** Bring up the flash register path and scan the ring for the newest record.
 *  Prints one "JOURNAL ..." summary line.  @return false if the flash failed. */
bool Journal_Init(void);

/** Append one record (erases the next sector when the ring enters it).
 *  @return false on a flash error or if Journal_Init failed. */
bool Journal_Append(uint8_t state, uint8_t source, int16_t temp_c100, uint32_t t_ms);

/** Print the newest n records, oldest first, one "LOG ..." line each. */
void Journal_PrintLast(unsigned n);

#endif /* JOURNAL_H */
