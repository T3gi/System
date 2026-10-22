/**
 * @file  console.c
 * @brief UART command interpreter, moved from main.c.  See console.h.
 */
#include "console.h"
#include "stream.h"
#include "ov7670.h"
#include "ov7670_capture.h"
#include "w25q64.h"
#include "platform_time.h"
#include "platform_board.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Set once FINIT has confirmed a real JEDEC ID. */
static int s_flash_ok = 0;
static uint32_t s_cmd_count = 0;

uint32_t Console_CommandCount(void) { return s_cmd_count; }

static void send_strip_named(const char *marker, int size, uint32_t width,
                             uint32_t start_line, uint32_t lines)
{
    ov7670_capture_stats_t st = OV7670_LastCapture();
    Stream_SendB64(marker, OV7670_Buffer(), size, width, start_line, lines, &st);
}

static void send_strip(int size, uint32_t width, uint32_t start_line, uint32_t lines)
{
    send_strip_named("STRIP", size, width, start_line, lines);
}

void Console_Help(void)
{
    printf("COMMANDS\r\n");
    printf("  PING                       -> PONG\r\n");
    printf("  ID                         read PID/VER over SCCB\r\n");
    printf("  MODE <n>                   0=VGA 1=CIF 2=QVGA 3=QCIF_BUILTIN\r\n");
    printf("                             4=QCIF_LINUX 5=CIF_FULLWIN 6=QVGA_FULLWIN\r\n");
    printf("                             7=VGA_FULLARRAY 656x488\r\n");
    printf("  MODES                      list modes\r\n");
    printf("  PROBE                      measure real HREF/PCLK geometry\r\n");
    printf("  FREEZE | AUTO              latch / release AEC+AGC+AWB\r\n");
    printf("  RD <reg>                   read register (hex)\r\n");
    printf("  SCCB [n] [attempts]        select back-end 0=HAL 1=HAL_MEM 2=BITBANG\r\n");
    printf("  SCCBTEST [n|-1] [reads] [attempts] [stopwait]  reliability test\r\n");
    printf("  WR <reg> <val>             write register (hex)\r\n");
    printf("  WIN <hs> <he> <vs> <ve>    set hardware window (decimal)\r\n");
    printf("  SNAP                       ONE frame, 2x2 averaged -> 176x144\r\n");
    printf("  FRAME [lineBytes] [lines] [verify]  capture+stream a whole frame\r\n");
    printf("  STRIP <lineBytes> <start> <lines>   capture + base64 one strip\r\n");
    printf("  DUP <lineBytes>            report duplicate-line distance\r\n");
    printf("  WARM <n>                   discard n frames\r\n");
    printf("  -- W25Q64 SPI flash on SPI2 (CS=PB12 SCK=PB13 MISO=PB14 MOSI=PB15) --\r\n");
    printf("  FINIT [0..7]               bring up SPI2 (0=21MHz .. 4=1.3MHz, default 4)\r\n");
    printf("  FID | FSTATUS | FSPEED <n> JEDEC id / status / clock\r\n");
    printf("  FTEST [addr]               erase+program+verify one page\r\n");
    printf("  FERASE [blocks]            erase N x 64KB (VGA frame needs 10)\r\n");
    printf("  FDUMP [addr] [len]         hex dump\r\n");
    printf("  FLASHREAD <addr> <len>     stream a range base64+CRC\r\n");
    printf("  RESET                      reboot the MCU\r\n");
}

static void cmd_modes(void)
{
    for (unsigned i = 0; i < ov7670_mode_count(); i++) {
        const ov7670_mode_t *m = ov7670_mode_info((int)i);
        printf("MODEDEF %u %s %ux%u\r\n", i, m->name, m->width, m->height);
    }
}

/**
 * @brief Capture a strip and report, for each line, how far away the nearest
 *        identical line is.  A distance of 1 means the sensor is doubling
 *        lines; 0 differences across the whole strip means a frozen/blank
 *        image.  This turns "why is the bottom half a copy of the top" into a
 *        measurement instead of a guess.
 */
static void cmd_dup(uint32_t line_bytes)
{
    int got = OV7670_CaptureStrip(line_bytes, 0, STRIP_LINES);
    if (got <= 0) { printf("ERR dup capture %d\r\n", got); return; }
    uint8_t *buf = OV7670_Buffer();
    uint32_t lines = (uint32_t)got / line_bytes;
    for (uint32_t i = 1; i < lines && i < 8; i++) {
        uint32_t d = OV7670_LineDiff(buf, buf + i * line_bytes, line_bytes);
        printf("DUP line0 vs line%lu diff=%lu/%lu\r\n",
               (unsigned long)i, (unsigned long)d, (unsigned long)line_bytes);
    }
}

void Console_CaptureFrame(void)
{
    const ov7670_mode_t *m = ov7670_mode_info(ov7670_current_mode());
    uint32_t lb = (uint32_t)m->width * 2u;
    printf("BUTTON capture mode=%s %ux%u\r\n", m->name, m->width, m->height);
    for (uint32_t y = 0; y < m->height; y += STRIP_LINES) {
        uint32_t n = m->height - y;
        if (n > STRIP_LINES) n = STRIP_LINES;
        int got = OV7670_CaptureStrip(lb, y, n);
        if (got < 0) { printf("ERR strip %d\r\n", got); break; }
        send_strip(got, m->width, y, n);
    }
    printf("OK\r\n");
}

void Console_Poll(void)
{
    static char     buf[96];
    static unsigned len = 0;

    int c = Platform_Console_GetChar();
    if (c < 0) return;

    if (c == '\r' || c == '\n') {
        if (len > 0) {
            buf[len] = '\0';
            len = 0;
            s_cmd_count++;
            /* Echo what we actually parsed: makes a mangled command
             * obvious instead of looking like a lost reply. */
            printf("CMD [%s]\r\n", buf);
            Console_Handle(buf);
        }
    } else if (len < sizeof(buf) - 1) {
        buf[len++] = (char)c;
    } else {
        len = 0;
        printf("ERR line too long\r\n");
    }
}

void Console_Handle(char *cmd)
{
    /* Tokenise in place. */
    char *argv[6] = {0};
    int   argc = 0;
    char *s = cmd;
    while (*s && argc < 6) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        argv[argc++] = s;
        while (*s && *s != ' ' && *s != '\t') s++;
        if (*s) *s++ = '\0';
    }
    if (argc == 0) return;

    /* Upper-case the verb so both "probe" and "PROBE" work. */
    for (char *c = argv[0]; *c; c++)
        if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);

    if (!strcmp(argv[0], "PING")) {
        printf("PONG build=%s %s mode=%d %s flash=%d rx=%lu ore=%lu cmds=%lu\r\n",
               __DATE__, __TIME__, ov7670_current_mode(),
               ov7670_mode_info(ov7670_current_mode())->name, s_flash_ok,
               (unsigned long)Platform_Console_RxBytes(),
               (unsigned long)Platform_Console_RxOverruns(),
               (unsigned long)s_cmd_count);
    } else if (!strcmp(argv[0], "HELP") || !strcmp(argv[0], "?")) {
        Console_Help();
    } else if (!strcmp(argv[0], "MODES")) {
        cmd_modes();
    } else if (!strcmp(argv[0], "ID")) {
        int h = OV7670_ReadReg(0x0A), l = OV7670_ReadReg(0x0B);
        printf("ID PID=0x%02X VER=0x%02X\r\n", h & 0xFF, l & 0xFF);
    } else if (!strcmp(argv[0], "MODE")) {
        if (argc < 2) { printf("ERR usage MODE <n>\r\n"); return; }
        int n = (int)strtol(argv[1], NULL, 0);
        if (n < 0 || n >= (int)ov7670_mode_count()) { printf("ERR bad mode\r\n"); return; }
        ov7670_configure(n);
        Platform_DelayMs(1200);             /* let AEC/AGC/AWB re-converge */
        for (int i = 0; i < 3; i++) (void)OV7670_SyncFrame(2000);
    } else if (!strcmp(argv[0], "PROBE")) {
        OV7670_ProbeGeometry();
    } else if (!strcmp(argv[0], "FREEZE")) {
        ov7670_freeze_auto(1);
    } else if (!strcmp(argv[0], "AUTO")) {
        ov7670_freeze_auto(0);
    } else if (!strcmp(argv[0], "RD")) {
        if (argc < 2) { printf("ERR usage RD <reg>\r\n"); return; }
        uint8_t r = (uint8_t)strtol(argv[1], NULL, 16);
        printf("RD 0x%02X = 0x%02X\r\n", r, OV7670_ReadReg(r) & 0xFF);
    } else if (!strcmp(argv[0], "WR")) {
        if (argc < 3) { printf("ERR usage WR <reg> <val>\r\n"); return; }
        uint8_t r = (uint8_t)strtol(argv[1], NULL, 16);
        uint8_t v = (uint8_t)strtol(argv[2], NULL, 16);
        OV7670_WriteReg(r, v);
        Platform_DelayMs(10);
        printf("WR 0x%02X = 0x%02X (readback 0x%02X)\r\n",
               r, v, OV7670_ReadReg(r) & 0xFF);
    } else if (!strcmp(argv[0], "SCCB")) {
        if (argc >= 2) {
            int b = (int)strtol(argv[1], NULL, 0);
            int rc = OV7670_BusSelect(b, argc >= 3 ? (int)strtol(argv[2], NULL, 0) : 3);
            if (rc == -2) { printf("ERR bad backend\r\n"); return; }
            if (rc < 0)   { printf("ERR init\r\n"); return; }
        }
        OV7670_BusReport();
    } else if (!strcmp(argv[0], "SCCBTEST")) {
        int which = (argc >= 2) ? (int)strtol(argv[1], NULL, 0) : -1;
        int n     = (argc >= 3) ? (int)strtol(argv[2], NULL, 0) : 200;
        int att   = (argc >= 4) ? (int)strtol(argv[3], NULL, 0) : 1;
        int stopw = (argc >= 5) ? (int)strtol(argv[4], NULL, 0) : 1;
        if (n < 1) n = 1;
        OV7670_BusTest(which, n, att, stopw);
    } else if (!strcmp(argv[0], "WIN")) {
        if (argc < 5) { printf("ERR usage WIN <hs> <he> <vs> <ve>\r\n"); return; }
        int hs = (int)strtol(argv[1], NULL, 0), he = (int)strtol(argv[2], NULL, 0);
        int vs = (int)strtol(argv[3], NULL, 0), ve = (int)strtol(argv[4], NULL, 0);
        ov7670_set_hw(hs, he, vs, ve);
        printf("WIN h[%d..%d] v[%d..%d]\r\n", hs, he, vs, ve);
    } else if (!strcmp(argv[0], "WARM")) {
        int n = (argc >= 2) ? (int)strtol(argv[1], NULL, 0) : 3;
        for (int i = 0; i < n; i++) (void)OV7670_SyncFrame(2000);
        printf("WARM %d frames\r\n", n);
    } else if (!strcmp(argv[0], "DUP")) {
        uint32_t lb = (argc >= 2) ? (uint32_t)strtol(argv[1], NULL, 0) : 352u;
        cmd_dup(lb);
    } else if (!strcmp(argv[0], "STRIP")) {
        if (argc < 4) { printf("ERR usage STRIP <lineBytes> <start> <lines>\r\n"); return; }
        uint32_t lb    = (uint32_t)strtol(argv[1], NULL, 0);
        uint32_t start = (uint32_t)strtol(argv[2], NULL, 0);
        uint32_t n     = (uint32_t)strtol(argv[3], NULL, 0);
        printf("DBG strip enter lb=%lu start=%lu n=%lu\r\n",
               (unsigned long)lb, (unsigned long)start, (unsigned long)n);
        int got = OV7670_CaptureStrip(lb, start, n);
        printf("DBG strip ret=%d\r\n", got);
        if (got < 0) { printf("ERR strip %d\r\n", got); return; }
        ov7670_capture_stats_t st = OV7670_LastCapture();
        printf("STRIPINFO lines=%lu dropouts=%lu bytes=%d\r\n",
               (unsigned long)st.lines, (unsigned long)st.dropouts, got);
        if (st.dropouts != 0) { printf("ERR dropouts\r\n"); return; }
        send_strip(got, lb / 2u, start, n);
    } else if (!strcmp(argv[0], "FRAME")) {
        /* Whole frame from one command: the firmware walks the strips itself
         * and streams each chunk.  Nothing is sent to the board between
         * chunks, so a frame can no longer be lost to a dropped command. */
        const ov7670_mode_t *m = ov7670_mode_info(ov7670_current_mode());
        uint32_t lb    = (argc >= 2) ? (uint32_t)strtol(argv[1], NULL, 0)
                                     : (uint32_t)m->width * 2u;
        uint32_t total = (argc >= 3) ? (uint32_t)strtol(argv[2], NULL, 0)
                                     : (uint32_t)m->height;
        if (lb == 0 || lb > MAX_LINE_BYTES || total == 0) {
            printf("ERR frame bad geometry lb=%lu lines=%lu\r\n",
                   (unsigned long)lb, (unsigned long)total);
            return;
        }
        uint32_t nstrips = (total + STRIP_LINES - 1u) / STRIP_LINES;
        printf("---FRAME-BEGIN w=%lu h=%lu strip=%u strips=%lu mode=%d %s---\r\n",
               (unsigned long)(lb / 2u), (unsigned long)total,
               (unsigned)STRIP_LINES, (unsigned long)nstrips,
               ov7670_current_mode(), m->name);
        uint32_t frame_crc = 0, frame_bytes = 0;
        for (uint32_t y = 0; y < total; y += STRIP_LINES) {
            uint32_t n = total - y;
            if (n > STRIP_LINES) n = STRIP_LINES;
            int got = OV7670_CaptureStrip(lb, y, n);
            if (got < 0) {
                ov7670_capture_stats_t st = OV7670_LastCapture();
                printf("ERR frame strip start=%lu ret=%d lines=%lu drop=%lu\r\n",
                       (unsigned long)y, got,
                       (unsigned long)st.lines, (unsigned long)st.dropouts);
                return;
            }
            frame_crc = Stream_Crc32Update(frame_crc, OV7670_Buffer(), (uint32_t)got);
            frame_bytes += (uint32_t)got;
            send_strip(got, lb / 2u, y, n);
        }
        /* Optional motion check: re-capture the very first strip now that the
         * frame is finished.  Because the strips come from different physical
         * frames spread over tens of seconds, any movement in the scene tears
         * the assembly at chunk boundaries.  Comparing this re-read against
         * the frame's first chunk turns "was the scene still?" into a
         * measurement the host can act on. */
        if (argc >= 4 && strtol(argv[3], NULL, 0) != 0) {
            uint32_t n = (total > STRIP_LINES) ? STRIP_LINES : total;
            int got = OV7670_CaptureStrip(lb, 0, n);
            if (got > 0)
                send_strip_named("VERIFY", got, lb / 2u, 0, n);
            else
                printf("DBG verify strip failed ret=%d\r\n", got);
        }
        printf("---FRAME-END bytes=%lu crc=%08lX---\r\n",
               (unsigned long)frame_bytes, (unsigned long)frame_crc);
    } else if (!strcmp(argv[0], "SNAP")) {
        /* One frame, averaged 2x2 in the MCU — immune to scene motion. */
        const ov7670_mode_t *m = ov7670_mode_info(ov7670_current_mode());
        uint32_t shortlines = 0;
        int got = OV7670_Snapshot(m->width, &shortlines);
        if (got < 0) {
            printf("ERR snap ret=%d (mode %s is %ux%u; need %ux%u)\r\n",
                   got, m->name, m->width, m->height, SNAP_W * 2u, SNAP_H * 2u);
            return;
        }
        printf("SNAPINFO out=%ux%u from=%ux%u bytes=%d short=%lu\r\n",
               (unsigned)SNAP_W, (unsigned)SNAP_H, m->width, m->height, got,
               (unsigned long)shortlines);
        send_strip_named("SNAP", got, SNAP_W, 0, SNAP_H);
    } else if (!strcmp(argv[0], "FINIT")) {
        /* Prescaler index: clock = APB1(42 MHz) / 2^(idx+1).
         * Default 4 (= /32, 1.3 MHz) because that is reliable on jumper wires;
         * raise it with FSPEED once FID is stable. */
        int idx = (argc >= 2) ? (int)strtol(argv[1], NULL, 0) : 4;
        if (idx < 0 || idx > 7) { printf("ERR presc 0..7\r\n"); return; }
        int rc = w25_init((uint32_t)idx);
        s_flash_ok = (rc == 0);
        uint8_t mfg = 0, type = 0, cap = 0;
        (void)w25_read_id(&mfg, &type, &cap);
        printf("FINIT rc=%d clk=%lu Hz id=%02X %02X %02X %s\r\n",
               rc, (unsigned long)(42000000u >> (idx + 1)), mfg, type, cap,
               (mfg == 0xEF) ? "[Winbond]" : "[no/bad response]");
        if (rc != 0)
            printf("FINIT hint: check 3V3, GND, WP#(pin3) and HOLD#(pin7) tied "
                   "HIGH, CS=PB12 SCK=PB13 MISO=PB14 MOSI=PB15\r\n");
    } else if (!strcmp(argv[0], "FID")) {
        uint8_t mfg = 0, type = 0, cap = 0;
        int rc = w25_read_id(&mfg, &type, &cap);
        printf("FID rc=%d %02X %02X %02X (expect EF 40 17)\r\n",
               rc, mfg, type, cap);
    } else if (!strcmp(argv[0], "FSPEED")) {
        if (argc < 2) { printf("ERR usage FSPEED 0..7\r\n"); return; }
        int idx = (int)strtol(argv[1], NULL, 0);
        if (idx < 0 || idx > 7) { printf("ERR presc 0..7\r\n"); return; }
        w25_set_speed((uint32_t)idx);
        uint8_t mfg = 0, type = 0, cap = 0;
        (void)w25_read_id(&mfg, &type, &cap);
        printf("FSPEED idx=%d -> %lu Hz, id=%02X %02X %02X\r\n",
               idx, (unsigned long)(42000000u >> (idx + 1)), mfg, type, cap);
    } else if (!strcmp(argv[0], "FSTATUS")) {
        uint8_t st = w25_read_status();
        printf("FSTATUS 0x%02X busy=%d wel=%d\r\n", st,
               (st & W25_SR1_BUSY) ? 1 : 0, (st & W25_SR1_WEL) ? 1 : 0);
    } else if (!strcmp(argv[0], "FTEST")) {
        uint32_t addr = (argc >= 2) ? (uint32_t)strtol(argv[1], NULL, 0) : 0u;
        int32_t bad = -1;
        uint32_t t0 = Platform_GetTick();
        int rc = w25_self_test(addr, &bad);
        printf("FTEST addr=0x%06lX rc=%d firstbad=%ld ms=%lu %s\r\n",
               (unsigned long)addr, rc, (long)bad,
               (unsigned long)(Platform_GetTick() - t0),
               (rc == 0) ? "[erase+program+verify OK]" : "[FAILED]");
    } else if (!strcmp(argv[0], "FERASE")) {
        /* Erase N 64 KB blocks from block 0.  A VGA frame needs 10. */
        uint32_t nblocks = (argc >= 2) ? (uint32_t)strtol(argv[1], NULL, 0) : 10u;
        uint32_t t0 = Platform_GetTick();
        for (uint32_t b = 0; b < nblocks; b++) {
            int rc = w25_erase_block(b * W25_BLOCK_SIZE);
            if (rc != 0) {
                printf("ERR erase block %lu rc=%d\r\n", (unsigned long)b, rc);
                return;
            }
            printf("FERASE block %lu/%lu done\r\n",
                   (unsigned long)(b + 1u), (unsigned long)nblocks);
        }
        printf("FERASE %lu blocks (%lu KB) in %lu ms\r\n",
               (unsigned long)nblocks, (unsigned long)(nblocks * 64u),
               (unsigned long)(Platform_GetTick() - t0));
    } else if (!strcmp(argv[0], "FDUMP")) {
        /* Small hex dump for eyeballing raw contents. */
        uint32_t addr = (argc >= 2) ? (uint32_t)strtol(argv[1], NULL, 0) : 0u;
        uint32_t len  = (argc >= 3) ? (uint32_t)strtol(argv[2], NULL, 0) : 64u;
        if (len > 256u) len = 256u;
        uint8_t tmp[256];
        int rc = w25_fast_read(addr, tmp, len);
        if (rc != 0) { printf("ERR fast_read rc=%d\r\n", rc); return; }
        for (uint32_t i = 0; i < len; i += 16) {
            printf("FDUMP %06lX:", (unsigned long)(addr + i));
            for (uint32_t j = 0; j < 16 && i + j < len; j++)
                printf(" %02X", tmp[i + j]);
            printf("\r\n");
        }
    } else if (!strcmp(argv[0], "FLASHREAD")) {
        /* Stream a range out of flash using the same base64+CRC32 chunk format
         * as the camera paths, so the host tooling needs no new parser.
         * The camera buffer is reused as scratch. */
        if (argc < 3) { printf("ERR usage FLASHREAD <addr> <len>\r\n"); return; }
        uint32_t addr = (uint32_t)strtol(argv[1], NULL, 0);
        uint32_t len  = (uint32_t)strtol(argv[2], NULL, 0);
        uint32_t chunk = CAM_BUF_BYTES;
        uint8_t *buf = OV7670_Buffer();
        printf("---FLASH-BEGIN addr=%lu len=%lu chunk=%lu---\r\n",
               (unsigned long)addr, (unsigned long)len, (unsigned long)chunk);
        uint32_t crc = 0;
        for (uint32_t off = 0; off < len; off += chunk) {
            uint32_t n = len - off;
            if (n > chunk) n = chunk;
            int rc = w25_fast_read(addr + off, buf, n);
            if (rc != 0) { printf("ERR fast_read rc=%d\r\n", rc); return; }
            crc = Stream_Crc32Update(crc, buf, n);
            ov7670_capture_stats_t st = OV7670_LastCapture();
            Stream_SendB64("STRIP", buf, (int)n, n, off, 1, &st);
        }
        printf("---FLASH-END bytes=%lu crc=%08lX---\r\n",
               (unsigned long)len, (unsigned long)crc);
    } else if (!strcmp(argv[0], "RESET")) {
        printf("RESET\r\nOK\r\n");
        Platform_SystemReset();
    } else {
        printf("ERR unknown '%s'\r\n", argv[0]);
    }
    printf("OK\r\n");
}
