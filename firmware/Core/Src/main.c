/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <string.h>
#include <stdlib.h>
#include "camera_blob.h"    /* register defines (REG_BRIGHT for SCCBTEST) */
#include "ov7670.h"
#include "w25q64.h"          /* external SPI NOR flash (frame storage) */
#include "mpu6050.h"
#include "gy89.h"
#include "i2c_scan.h"
#include "servo.h"
#include "platform_sccb.h"
#include "platform_camera.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#ifndef SCCB_BOOT_BACKEND
#define SCCB_BOOT_BACKEND SCCB_HAL
#endif
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define UART_BAUD  921600u

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

SPI_HandleTypeDef hspi2;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim3;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI2_Init(void);
static void MX_TIM3_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM1_XCLK_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int _write(int file, char *ptr, int len) {
    extern UART_HandleTypeDef huart2;
    HAL_UART_Transmit(&huart2, (uint8_t*)ptr, len, HAL_MAX_DELAY);
    return len;
}

/* Set once FINIT has confirmed a real JEDEC ID. */
static int s_flash_ok = 0;

/**
 * @brief CRC-32 (IEEE 802.3, the same polynomial Python's zlib.crc32 uses)
 *        so the host can verify every strip and re-request corrupted ones
 *        instead of silently assembling a torn image.
 */
static uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n)
{
    crc = ~crc;
    for (uint32_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

static uint32_t crc32_buf(const uint8_t *p, uint32_t n)
{
    return crc32_update(0, p, n);
}

/**
 * @brief Base64-encode `size` bytes of the strip buffer between machine
 *        parsable markers.
 */
static void send_b64_named(const char *marker, const uint8_t *src, int size,
                           uint32_t width, uint32_t start_line, uint32_t lines)
{
    static const char alpha[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    printf("---%s-BEGIN w=%lu start=%lu lines=%lu bytes=%d crc=%08lX "
           "got=%lu drop=%lu linemin=%lu linemax=%lu short=%lu---\r\n",
           marker, (unsigned long)width, (unsigned long)start_line,
           (unsigned long)lines, size,
           (unsigned long)crc32_buf(src, (uint32_t)size),
           (unsigned long)cam_last_lines_captured,
           (unsigned long)cam_last_href_dropouts,
           (unsigned long)cam_last_line_min, (unsigned long)cam_last_line_max,
           (unsigned long)cam_last_short_lines);

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

    size_t rem = end - p;
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

static void cam_send_strip_named(const char *marker, int size, uint32_t width,
                                 uint32_t start_line, uint32_t lines)
{
    send_b64_named(marker, cam_framebuffer, size, width, start_line, lines);
}

static void cam_send_strip(int size, uint32_t width, uint32_t start_line, uint32_t lines)
{
    cam_send_strip_named("STRIP", size, width, start_line, lines);
}


/* --- Retarget printf() --------------------------------------------------- */
int __io_putchar(int ch)
{
    uint8_t b = (uint8_t)ch;
    HAL_UART_Transmit(&huart2, &b, 1, HAL_MAX_DELAY);
    return ch;
}

/* ==================================================================
 * UART command interpreter.
 *
 * Everything the host automation needs is driven from here, so a full
 * experiment sweep (mode change, geometry probe, register poke, strip
 * capture) never requires a reflash.  Each command answers with exactly one
 * terminating "OK" or "ERR ..." line, which is what the host waits on.
 */

/* RX diagnostics — surfaced by PING so a flaky link is visible as a number
 * rather than inferred from missing replies. */
static volatile uint32_t s_rx_bytes    = 0;
static volatile uint32_t s_rx_overruns = 0;
static volatile uint32_t s_cmd_count   = 0;

static int uart_getchar_nb(void)
{
    uint32_t sr = huart2.Instance->SR;
    if (sr & (USART_SR_ORE | USART_SR_RXNE)) {
        /* Reading DR clears both RXNE and ORE.  Crucially the byte in DR is
         * still valid data (the overrun destroyed an *earlier* byte), so it
         * must be returned — the previous version discarded it, which could
         * silently eat the newline that terminates a command. */
        int d = (int)(huart2.Instance->DR & 0xFFu);
        if (sr & USART_SR_ORE) s_rx_overruns++;
        s_rx_bytes++;
        return d;
    }
    return -1;
}

/* ======================================================================= */
/* SCCB back-end self-test                                                  */
/* ======================================================================= */
static void dwt_enable(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

/**
 * Exercise one back-end on the live sensor:
 *   - first read right after (re)init (the "warm-up" issue from lab2),
 *   - N reads of PID/VER (0x0A/0x0B) -> exact-match count + time per read,
 *   - 32 writes to BRIGHT (0x55) verified with the same back-end AND with the
 *     bit-bang reference (so a broken read path can't hide a working write),
 *   - HAL error code / bit-bang 9th-bit statistics.
 * The original BRIGHT value is restored afterwards.
 */
static void sccb_test_backend(sccb_backend_t b, int n, int attempts)
{
    const uint8_t A = 0x21;
    sccb_init(SCCB_BITBANG);
    int bright0 = sccb_read_reg(A, REG_BRIGHT);

    sccb_set_attempts(attempts);
    sccb_init(b);
    sccb_stats_reset();

    int f_h = sccb_read_reg(A, 0x0A), f_l = sccb_read_reg(A, 0x0B);

    int ok = 0, bad = 0, err = 0;
    uint32_t cyc = 0;
    for (int i = 0; i < n; i++) {
        uint32_t t0 = DWT->CYCCNT;
        int h = sccb_read_reg(A, 0x0A);
        int l = sccb_read_reg(A, 0x0B);
        cyc += DWT->CYCCNT - t0;
        if (h < 0 || l < 0) err++;
        else if (h == 0x76 && l == 0x73) ok++;
        else bad++;
    }
    sccb_stats_t rd = *sccb_stats();

    int w_err = 0, w_self = 0, w_ref = 0;
    for (int i = 0; i < 32; i++) {
        uint8_t v = (uint8_t)(i * 8 + 3);
        sccb_init(b);
        if (sccb_write_reg(A, REG_BRIGHT, v) < 0) { w_err++; continue; }
        if (sccb_read_reg(A, REG_BRIGHT) == v) w_self++;
        sccb_init(SCCB_BITBANG);
        if (sccb_read_reg(A, REG_BRIGHT) == v) w_ref++;
    }

    printf("SCCBTEST %d %s\r\n", (int)b, sccb_backend_name(b));
    printf("  first-read  PID=%d VER=%d %s\r\n", f_h, f_l,
           (f_h == 0x76 && f_l == 0x73) ? "OK" : "WRONG");
    printf("  reads       %d/%d OK  wrong=%d  error=%d  %lu us/read  retries=%lu last_err=0x%lX\r\n",
           ok, n, bad, err,
           (unsigned long)(cyc / (uint32_t)(2 * n) / (SystemCoreClock / 1000000u)),
           (unsigned long)rd.retries, (unsigned long)rd.last_err);
    printf("  writes      32 sent  hal_err=%d  readback_self=%d/32  readback_bitbang=%d/32\r\n",
           w_err, w_self, w_ref);
    if (b == SCCB_BITBANG)
        printf("  9th bit     LOW(ack)=%lu HIGH(no-ack)=%lu\r\n",
               (unsigned long)rd.x_low, (unsigned long)rd.x_high);

    sccb_init(SCCB_BITBANG);
    if (bright0 >= 0) sccb_write_reg(A, REG_BRIGHT, (uint8_t)bright0);
}

static void cmd_sccbtest(int which, int n, int attempts)
{
    sccb_backend_t keep = sccb_backend();
    dwt_enable();
    for (int b = 0; b < SCCB_BACKEND_COUNT; b++)
        if (which < 0 || which == b) sccb_test_backend((sccb_backend_t)b, n, attempts);
    sccb_set_attempts(3);
    sccb_init(keep);
    printf("SCCB backend restored: %s\r\n", sccb_backend_name(keep));
}

static void cmd_help(void)
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
    int got = cam_capture_strip(line_bytes, 0, STRIP_LINES);
    if (got <= 0) { printf("ERR dup capture %d\r\n", got); return; }
    uint32_t lines = (uint32_t)got / line_bytes;
    for (uint32_t i = 1; i < lines && i < 8; i++) {
        uint32_t d = cam_line_diff(cam_framebuffer,
                                   cam_framebuffer + i * line_bytes, line_bytes);
        printf("DUP line0 vs line%lu diff=%lu/%lu\r\n",
               (unsigned long)i, (unsigned long)d, (unsigned long)line_bytes);
    }
}

static void handle_command(char *cmd)
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
               (unsigned long)s_rx_bytes, (unsigned long)s_rx_overruns,
               (unsigned long)s_cmd_count);
    } else if (!strcmp(argv[0], "HELP") || !strcmp(argv[0], "?")) {
        cmd_help();
    } else if (!strcmp(argv[0], "MODES")) {
        cmd_modes();
    } else if (!strcmp(argv[0], "ID")) {
        int h = sccb_read_reg(0x21, 0x0A), l = sccb_read_reg(0x21, 0x0B);
        printf("ID PID=0x%02X VER=0x%02X\r\n", h & 0xFF, l & 0xFF);
    } else if (!strcmp(argv[0], "MODE")) {
        if (argc < 2) { printf("ERR usage MODE <n>\r\n"); return; }
        int n = (int)strtol(argv[1], NULL, 0);
        if (n < 0 || n >= (int)ov7670_mode_count()) { printf("ERR bad mode\r\n"); return; }
        ov7670_configure(n);
        HAL_Delay(1200);                    /* let AEC/AGC/AWB re-converge */
        for (int i = 0; i < 3; i++) (void)cam_sync_frame_start(2000);
    } else if (!strcmp(argv[0], "PROBE")) {
        cam_probe();
    } else if (!strcmp(argv[0], "FREEZE")) {
        ov7670_freeze_auto(1);
    } else if (!strcmp(argv[0], "AUTO")) {
        ov7670_freeze_auto(0);
    } else if (!strcmp(argv[0], "RD")) {
        if (argc < 2) { printf("ERR usage RD <reg>\r\n"); return; }
        uint8_t r = (uint8_t)strtol(argv[1], NULL, 16);
        printf("RD 0x%02X = 0x%02X\r\n", r, sccb_read_reg(0x21, r) & 0xFF);
    } else if (!strcmp(argv[0], "WR")) {
        if (argc < 3) { printf("ERR usage WR <reg> <val>\r\n"); return; }
        uint8_t r = (uint8_t)strtol(argv[1], NULL, 16);
        uint8_t v = (uint8_t)strtol(argv[2], NULL, 16);
        sccb_write_reg(0x21, r, v);
        HAL_Delay(10);
        printf("WR 0x%02X = 0x%02X (readback 0x%02X)\r\n",
               r, v, sccb_read_reg(0x21, r) & 0xFF);
    } else if (!strcmp(argv[0], "SCCB")) {
        if (argc >= 2) {
            int b = (int)strtol(argv[1], NULL, 0);
            if (b < 0 || b >= SCCB_BACKEND_COUNT) { printf("ERR bad backend\r\n"); return; }
            sccb_set_attempts(argc >= 3 ? (int)strtol(argv[2], NULL, 0) : 3);
            if (sccb_init((sccb_backend_t)b) < 0) { printf("ERR init\r\n"); return; }
        }
        const sccb_stats_t *st = sccb_stats();
        printf("SCCB %d %s xfers=%lu errors=%lu retries=%lu last_err=0x%lX\r\n",
               (int)sccb_backend(), sccb_backend_name(sccb_backend()),
               (unsigned long)st->xfers, (unsigned long)st->errors,
               (unsigned long)st->retries, (unsigned long)st->last_err);
    } else if (!strcmp(argv[0], "SCCBTEST")) {
        int which = (argc >= 2) ? (int)strtol(argv[1], NULL, 0) : -1;
        int n     = (argc >= 3) ? (int)strtol(argv[2], NULL, 0) : 200;
        int att   = (argc >= 4) ? (int)strtol(argv[3], NULL, 0) : 1;
        sccb_set_stop_wait(argc >= 5 ? (int)strtol(argv[4], NULL, 0) : 1);
        if (n < 1) n = 1;
        cmd_sccbtest(which, n, att);
        sccb_set_stop_wait(1);
    } else if (!strcmp(argv[0], "WIN")) {
        if (argc < 5) { printf("ERR usage WIN <hs> <he> <vs> <ve>\r\n"); return; }
        int hs = (int)strtol(argv[1], NULL, 0), he = (int)strtol(argv[2], NULL, 0);
        int vs = (int)strtol(argv[3], NULL, 0), ve = (int)strtol(argv[4], NULL, 0);
        ov7670_set_hw(hs, he, vs, ve);
        printf("WIN h[%d..%d] v[%d..%d]\r\n", hs, he, vs, ve);
    } else if (!strcmp(argv[0], "WARM")) {
        int n = (argc >= 2) ? (int)strtol(argv[1], NULL, 0) : 3;
        for (int i = 0; i < n; i++) (void)cam_sync_frame_start(2000);
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
        int got = cam_capture_strip(lb, start, n);
        printf("DBG strip ret=%d\r\n", got);
        if (got < 0) { printf("ERR strip %d\r\n", got); return; }
        printf("STRIPINFO lines=%lu dropouts=%lu bytes=%d\r\n",
               (unsigned long)cam_last_lines_captured,
               (unsigned long)cam_last_href_dropouts, got);
        if (cam_last_href_dropouts != 0) { printf("ERR dropouts\r\n"); return; }
        cam_send_strip(got, lb / 2u, start, n);
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
            int got = cam_capture_strip(lb, y, n);
            if (got < 0) {
                printf("ERR frame strip start=%lu ret=%d lines=%lu drop=%lu\r\n",
                       (unsigned long)y, got,
                       (unsigned long)cam_last_lines_captured,
                       (unsigned long)cam_last_href_dropouts);
                return;
            }
            frame_crc = crc32_update(frame_crc, cam_framebuffer, (uint32_t)got);
            frame_bytes += (uint32_t)got;
            cam_send_strip(got, lb / 2u, y, n);
        }
        /* Optional motion check: re-capture the very first strip now that the
         * frame is finished.  Because the strips come from different physical
         * frames spread over tens of seconds, any movement in the scene tears
         * the assembly at chunk boundaries.  Comparing this re-read against
         * the frame's first chunk turns "was the scene still?" into a
         * measurement the host can act on. */
        if (argc >= 4 && strtol(argv[3], NULL, 0) != 0) {
            uint32_t n = (total > STRIP_LINES) ? STRIP_LINES : total;
            int got = cam_capture_strip(lb, 0, n);
            if (got > 0)
                cam_send_strip_named("VERIFY", got, lb / 2u, 0, n);
            else
                printf("DBG verify strip failed ret=%d\r\n", got);
        }
        printf("---FRAME-END bytes=%lu crc=%08lX---\r\n",
               (unsigned long)frame_bytes, (unsigned long)frame_crc);
    } else if (!strcmp(argv[0], "SNAP")) {
        /* One frame, averaged 2x2 in the MCU — immune to scene motion. */
        const ov7670_mode_t *m = ov7670_mode_info(ov7670_current_mode());
        uint32_t shortlines = 0;
        int got = cam_snapshot(m->width, &shortlines);
        if (got < 0) {
            printf("ERR snap ret=%d (mode %s is %ux%u; need %ux%u)\r\n",
                   got, m->name, m->width, m->height, SNAP_W * 2u, SNAP_H * 2u);
            return;
        }
        printf("SNAPINFO out=%ux%u from=%ux%u bytes=%d short=%lu\r\n",
               (unsigned)SNAP_W, (unsigned)SNAP_H, m->width, m->height, got,
               (unsigned long)shortlines);
        cam_send_strip_named("SNAP", got, SNAP_W, 0, SNAP_H);
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
        uint32_t t0 = HAL_GetTick();
        int rc = w25_self_test(addr, &bad);
        printf("FTEST addr=0x%06lX rc=%d firstbad=%ld ms=%lu %s\r\n",
               (unsigned long)addr, rc, (long)bad,
               (unsigned long)(HAL_GetTick() - t0),
               (rc == 0) ? "[erase+program+verify OK]" : "[FAILED]");
    } else if (!strcmp(argv[0], "FERASE")) {
        /* Erase N 64 KB blocks from block 0.  A VGA frame needs 10. */
        uint32_t nblocks = (argc >= 2) ? (uint32_t)strtol(argv[1], NULL, 0) : 10u;
        uint32_t t0 = HAL_GetTick();
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
               (unsigned long)(HAL_GetTick() - t0));
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
         * as the camera paths, so the host tooling needs no new parser. */
        if (argc < 3) { printf("ERR usage FLASHREAD <addr> <len>\r\n"); return; }
        uint32_t addr = (uint32_t)strtol(argv[1], NULL, 0);
        uint32_t len  = (uint32_t)strtol(argv[2], NULL, 0);
        uint32_t chunk = CAM_BUF_BYTES;
        printf("---FLASH-BEGIN addr=%lu len=%lu chunk=%lu---\r\n",
               (unsigned long)addr, (unsigned long)len, (unsigned long)chunk);
        uint32_t crc = 0;
        for (uint32_t off = 0; off < len; off += chunk) {
            uint32_t n = len - off;
            if (n > chunk) n = chunk;
            int rc = w25_fast_read(addr + off, cam_framebuffer, n);
            if (rc != 0) { printf("ERR fast_read rc=%d\r\n", rc); return; }
            crc = crc32_update(crc, cam_framebuffer, n);
            send_b64_named("STRIP", cam_framebuffer, (int)n, n, off, 1);
        }
        printf("---FLASH-END bytes=%lu crc=%08lX---\r\n",
               (unsigned long)len, (unsigned long)crc);
    } else if (!strcmp(argv[0], "RESET")) {
        printf("RESET\r\nOK\r\n");
        while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC) == RESET) {}
        NVIC_SystemReset();
    } else {
        printf("ERR unknown '%s'\r\n", argv[0]);
    }
    printf("OK\r\n");
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_SPI2_Init();
  MX_TIM3_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("hello from STM32\r\n");
  printf("\r\n=== Запуск перевірки компонентів ===\r\n");

  I2C_Scan();
  Servo_Init();
  Servo_SetAngle(0);
  HAL_Delay(1000);
  Servo_SetAngle(90);
  HAL_Delay(1000);
  Servo_SetAngle(180);
  HAL_Delay(1000);

  W25Q64_ReadID();

  MPU6050_Init();

  GY89_ReadIDs();

  printf("===================================\r\n");
  sccb_pins_reinit();
  printf("\r\n========================================\r\n");
    printf(" STM32F411RE + OV7670 strip-capture rig [lab2-sccb-hal]\r\n");
    printf(" SYSCLK=%lu HCLK=%lu PCLK1=%lu PCLK2=%lu\r\n",
           (unsigned long)HAL_RCC_GetSysClockFreq(),
           (unsigned long)HAL_RCC_GetHCLKFreq(),
           (unsigned long)HAL_RCC_GetPCLK1Freq(),
           (unsigned long)HAL_RCC_GetPCLK2Freq());
    printf(" Build = %s %s   UART = %lu 8N1 (fixed)\r\n", __DATE__, __TIME__,
           (unsigned long)UART_BAUD);
    printf(" Strip buffer = %u bytes (%u lines x %u max)\r\n",
           (unsigned)STRIP_BYTES, (unsigned)STRIP_LINES, (unsigned)MAX_LINE_BYTES);
    printf("========================================\r\n");

    /* XCLK must run before the sensor will respond on SCCB. */
    MX_TIM1_XCLK_Init();
    printf(">> XCLK = %lu Hz on PA8 (TIM1_CH1, 50%% duty)\r\n",
           (unsigned long)(HAL_RCC_GetPCLK2Freq() / 4));
    HAL_Delay(100);

    Camera_HardReset();
    Camera_SetPwdn(0);

    /* SCCB through stock HAL I2C (see App/Src/sccb.c). */
    sccb_init(SCCB_BOOT_BACKEND);
    printf(">> SCCB backend = %s\r\n", sccb_backend_name(sccb_backend()));
    HAL_Delay(50);

    int ov7670_ok = 0;
    for (int rep = 0; rep < 5 && !ov7670_ok; rep++) {
        int h = sccb_read_reg(0x21, 0x0A);
        int l = sccb_read_reg(0x21, 0x0B);
        printf(">> SCCB probe #%d: PID=%d(0x%02X) VER=%d(0x%02X) halerr=0x%lX%s\r\n",
               rep, h, h & 0xFF, l, l & 0xFF,
               (unsigned long)sccb_stats()->last_err,
               (h == 0x76 && l == 0x73) ? "  [OV7670 OK]" : "");
        if (h == 0x76 && l == 0x73) ov7670_ok = 1;
        HAL_Delay(50);
    }

    if (ov7670_ok) {
        ov7670_configure(ov7670_current_mode());
        HAL_Delay(1200);                   /* AWB/AEC convergence */
    } else {
        printf("ERR OV7670 not detected on SCCB\r\n");
    }

    printf("READY\r\n");
    cmd_help();
    printf("OK\r\n");

    /* Command loop.  LD2 blinks slowly so it is obvious the rig is alive,
     * but nothing is printed unsolicited — the host parses this stream. */
    char     buf[96];
    unsigned len = 0;
    uint32_t last_blink = HAL_GetTick();
    uint8_t  btn_prev = 1;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    
    int c = uart_getchar_nb();
    if (c >= 0) {
        if (c == '\r' || c == '\n') {
            if (len > 0) {
                buf[len] = '\0';
                len = 0;
                s_cmd_count++;
                /* Echo what we actually parsed: makes a mangled command
                    * obvious instead of looking like a lost reply. */
                printf("CMD [%s]\r\n", buf);
                handle_command(buf);
            }
        } else if (len < sizeof(buf) - 1) {
            buf[len++] = (char)c;
        } else {
            len = 0;
            printf("ERR line too long\r\n");
        }
    }

    /* USER button still works as a manual trigger: grab a full frame of
        * the current mode using as many strips as needed. */
    uint8_t btn = HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin);
    if (btn == 0 && btn_prev == 1) {
        const ov7670_mode_t *m = ov7670_mode_info(ov7670_current_mode());
        uint32_t lb = (uint32_t)m->width * 2u;
        printf("BUTTON capture mode=%s %ux%u\r\n", m->name, m->width, m->height);
        for (uint32_t y = 0; y < m->height; y += STRIP_LINES) {
            uint32_t n = m->height - y;
            if (n > STRIP_LINES) n = STRIP_LINES;
            int got = cam_capture_strip(lb, y, n);
            if (got < 0) { printf("ERR strip %d\r\n", got); break; }
            cam_send_strip(got, m->width, y, n);
        }
        printf("OK\r\n");
    }
    btn_prev = btn;

    if (HAL_GetTick() - last_blink > 1000) {
        last_blink = HAL_GetTick();
        HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
        /* Idle heartbeat.  Printed only from the command loop, so it can
            * never interleave with a strip payload.  If these stop while the
            * board is otherwise silent, the main loop itself is stuck; if
            * they continue but commands go unanswered, the RX path is at
            * fault.  Distinguishing those two took far too long without it. */
        printf("TICK t=%lu rx=%lu ore=%lu cmds=%lu\r\n",
                (unsigned long)last_blink, (unsigned long)s_rx_bytes,
                (unsigned long)s_rx_overruns, (unsigned long)s_cmd_count);
        float temp = 24.68f;

        if (MPU6050_ReadTemperature(&temp)) {
            int32_t int_part = (int32_t)temp;
            // Отримуємо 2 знаки після коми (множимо на 100)
            int32_t frac_part = (int32_t)((temp - (float)int_part) * 100.0f);

            // Враховуємо випадок від'ємних температур
            if (frac_part < 0) {
                frac_part = -frac_part;
            }

            printf("[MPU6050] Температура: %ld.%02ld °C\r\n", (long)int_part, (long)frac_part);
        } else {
            printf("[MPU6050] Помилка отримання температури!\r\n");
    }
        
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_XCLK_Init(void)
{
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA8 -> AF1 TIM1_CH1, push-pull, very high speed. */
    GPIO_InitTypeDef g = {0};
    g.Pin       = GPIO_PIN_8;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOA, &g);

    htim1.Instance               = TIM1;
    htim1.Init.Prescaler         = 0;
    htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim1.Init.Period            = 3;         /* ARR — 4 ticks total */
    htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim1) != HAL_OK) Error_Handler();

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode      = TIM_OCMODE_PWM1;
    oc.Pulse       = 2;                        /* CCR — 50 % duty */
    oc.OCPolarity  = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCFastMode  = TIM_OCFAST_DISABLE;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_1) != HAL_OK) Error_Handler();

    /* PWM_Start on advanced-timer TIM1 also sets MOE (main output enable). */
    if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK) Error_Handler();
}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 65535;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1|GPIO_PIN_2, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PC0 PC1 PC2 PC3
                           PC4 PC5 PC6 PC7 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA0 PA1 PA4 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PB1 PB2 PB12 */
  GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  printf("Some error has happened\r\n");
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
