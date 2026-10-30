/**
 * @file  journal.c
 * @brief Event journal in the W25Q64.  See journal.h for the record format.
 */
#include "journal.h"
#include "damper.h"
#include "stream.h"
#include "w25q64.h"
#include <stdio.h>
#include <string.h>

#define SLOTS_PER_SECTOR  (W25_SECTOR_SIZE / JOURNAL_RECORD_SIZE)      /* 256 */
#define JOURNAL_SLOTS     (JOURNAL_SECTORS * SLOTS_PER_SECTOR)        /* 1024 */
#define JOURNAL_BASE      (W25_TOTAL_BYTES - JOURNAL_SECTORS * W25_SECTOR_SIZE)
#define FLASH_SPI_BR      4      /* 1.3 MHz: the FINIT default, safe on jumper wires */
#define MAX_PRINT         16     /* most records Journal_PrintLast can show */

static bool     s_ok;
static uint32_t s_next_slot;     /* where the next record goes */
static uint32_t s_next_seq;      /* its number */
static uint32_t s_last_slot;     /* newest record (valid if s_count > 0) */
static uint32_t s_count;         /* records found at boot + appended since */

static uint32_t slot_addr(uint32_t slot)
{
    return JOURNAL_BASE + slot * JOURNAL_RECORD_SIZE;
}

/* An erased slot reads all 0xFF. */
static bool is_erased(const uint8_t *r)
{
    for (uint32_t i = 0; i < JOURNAL_RECORD_SIZE; i++) {
        if (r[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

/* Unpack a record.  Returns false if the CRC does not match. */
static bool decode(const uint8_t *r, journal_entry_t *e)
{
    uint32_t crc;
    memcpy(&crc, &r[12], 4);
    if (crc != Stream_Crc32Update(0, r, 12)) {
        return false;
    }
    memcpy(&e->seq,       &r[0], 4);
    memcpy(&e->t_ms,      &r[4], 4);
    memcpy(&e->temp_c100, &r[8], 2);
    e->state  = r[10];
    e->source = r[11];
    return true;
}

bool Journal_Init(void)
{
    uint8_t page[W25_PAGE_SIZE];
    uint32_t bad = 0;
    uint32_t max_seq = 0;

    s_ok = false;
    s_count = 0;
    s_next_slot = 0;
    s_next_seq = 1;

    int rc = w25_init(FLASH_SPI_BR);
    if (rc != 0) {
        printf("JOURNAL ERR flash init rc=%d\r\n", rc);
        return false;
    }

    /* Read the whole journal area page by page and find the newest record. */
    for (uint32_t slot = 0; slot < JOURNAL_SLOTS; slot++) {
        uint32_t off = (slot * JOURNAL_RECORD_SIZE) % W25_PAGE_SIZE;
        if (off == 0) {
            if (w25_fast_read(slot_addr(slot), page, W25_PAGE_SIZE) != 0) {
                printf("JOURNAL ERR read slot %lu\r\n", (unsigned long)slot);
                return false;
            }
        }
        journal_entry_t e;
        if (is_erased(&page[off])) {
            continue;
        }
        if (!decode(&page[off], &e)) {
            bad++;               /* e.g. power was cut during the write */
            continue;
        }
        s_count++;
        if (e.seq >= max_seq) {
            max_seq = e.seq;
            s_last_slot = slot;
        }
    }

    if (s_count > 0) {
        s_next_slot = (s_last_slot + 1) % JOURNAL_SLOTS;
        s_next_seq  = max_seq + 1;
    }
    s_ok = true;
    printf("JOURNAL records=%lu bad=%lu next_seq=%lu\r\n",
           (unsigned long)s_count, (unsigned long)bad, (unsigned long)s_next_seq);
    return true;
}

bool Journal_Append(uint8_t state, uint8_t source, int16_t temp_c100, uint32_t t_ms)
{
    uint8_t r[JOURNAL_RECORD_SIZE];

    if (!s_ok) {
        return false;
    }

    /* Find a free slot.  A slot at the start of a sector: erase the sector
     * first (it may hold records from the previous lap of the ring).  Any
     * other slot: use it if it is still erased, otherwise step over it (a
     * record half-written before a power cut). */
    for (uint32_t tries = 0; tries < SLOTS_PER_SECTOR; tries++) {
        if (s_next_slot % SLOTS_PER_SECTOR == 0) {
            if (w25_erase_sector(slot_addr(s_next_slot)) != 0) {
                return false;
            }
            break;
        }
        if (w25_fast_read(slot_addr(s_next_slot), r, JOURNAL_RECORD_SIZE) != 0) {
            return false;
        }
        if (is_erased(r)) {
            break;
        }
        s_next_slot = (s_next_slot + 1) % JOURNAL_SLOTS;
    }

    /* Pack the record: bytes 0..11 are the data, 12..15 their CRC. */
    memcpy(&r[0], &s_next_seq, 4);
    memcpy(&r[4], &t_ms, 4);
    memcpy(&r[8], &temp_c100, 2);
    r[10] = state;
    r[11] = source;
    uint32_t crc = Stream_Crc32Update(0, r, 12);
    memcpy(&r[12], &crc, 4);

    if (w25_page_program(slot_addr(s_next_slot), r, JOURNAL_RECORD_SIZE) != 0) {
        return false;
    }

    s_last_slot = s_next_slot;
    s_next_slot = (s_next_slot + 1) % JOURNAL_SLOTS;
    s_next_seq++;
    s_count++;
    return true;
}

static void print_entry(const journal_entry_t *e)
{
    const char *state = "CLOSED";
    const char *source = "NONE";
    int32_t t = e->temp_c100;
    const char *sign = "";

    if (e->state == DAMPER_OPEN) {
        state = "OPEN";
    }
    if (e->source == DAMPER_SRC_TEMP) {
        source = "TEMP";
    } else if (e->source == DAMPER_SRC_CAMERA) {
        source = "CAMERA";
    }
    if (t < 0) {
        sign = "-";
        t = -t;
    }
    printf("LOG seq=%lu t=%lu state=%s src=%s temp=%s%ld.%02ld\r\n",
           (unsigned long)e->seq, (unsigned long)e->t_ms, state, source,
           sign, (long)(t / 100), (long)(t % 100));
}

void Journal_PrintLast(unsigned n)
{
    journal_entry_t last[MAX_PRINT];
    unsigned got = 0;
    uint32_t slot = s_last_slot;
    uint32_t prev_seq = 0xFFFFFFFF;

    if (!s_ok || s_count == 0) {
        printf("LOG empty\r\n");
        return;
    }
    if (n > MAX_PRINT) {
        n = MAX_PRINT;
    }

    /* Go backwards from the newest record.  Every older record has a smaller
     * number; a bigger one means we went round the whole ring, so stop. */
    for (uint32_t i = 0; i < JOURNAL_SLOTS && got < n; i++) {
        uint8_t r[JOURNAL_RECORD_SIZE];
        journal_entry_t e;

        if (w25_fast_read(slot_addr(slot), r, JOURNAL_RECORD_SIZE) != 0) {
            break;
        }
        if (!is_erased(r) && decode(r, &e)) {
            if (e.seq >= prev_seq) {
                break;
            }
            prev_seq = e.seq;
            last[got] = e;
            got++;
        }
        if (slot == 0) {
            slot = JOURNAL_SLOTS - 1;
        } else {
            slot--;
        }
    }

    /* last[] holds the newest first; print the oldest first. */
    while (got > 0) {
        got--;
        print_entry(&last[got]);
    }
}
