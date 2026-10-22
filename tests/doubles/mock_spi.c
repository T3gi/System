#include "mock_spi.h"
#include "platform_spi.h"
#include "unity.h"
#include <stdio.h>
#include <string.h>

#define MAX_FRAMES 64
#define MAX_BYTES  16

typedef struct {
    uint8_t tx[MAX_BYTES], rx[MAX_BYTES];
    size_t  n;
} frame_t;

static frame_t s_exp[MAX_FRAMES];
static size_t  s_count, s_cur, s_byte;
static int     s_cs;
static char    s_err[200];

static void fail(const char *msg)
{
    if (s_err[0] == '\0') snprintf(s_err, sizeof s_err, "frame %u: %s", (unsigned)s_cur, msg);
}

void mock_spi_reset(void)
{
    s_count = s_cur = s_byte = 0;
    s_cs = 0;
    s_err[0] = '\0';
}

void mock_spi_expect(const uint8_t *tx, size_t n, const uint8_t *rx)
{
    TEST_ASSERT_TRUE_MESSAGE(s_count < MAX_FRAMES && n <= MAX_BYTES, "mock_spi: too many expectations");
    frame_t *f = &s_exp[s_count++];
    memcpy(f->tx, tx, n);
    if (rx) memcpy(f->rx, rx, n); else memset(f->rx, 0xFF, n);
    f->n = n;
}

void mock_spi_verify(void)
{
    if (s_err[0] == '\0' && s_cur != s_count) {
        snprintf(s_err, sizeof s_err, "only %u of %u expected frames were sent",
                 (unsigned)s_cur, (unsigned)s_count);
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", s_err, "SPI sequence differs from the expected one");
}

void Platform_SPI_SetCS(bool active)
{
    if (active && !s_cs) {
        s_byte = 0;
        if (s_cur >= s_count) fail("unexpected extra frame");
    } else if (!active && s_cs) {
        if (s_cur < s_count && s_byte != s_exp[s_cur].n) {
            char m[80];
            snprintf(m, sizeof m, "%u bytes sent, %u expected", (unsigned)s_byte, (unsigned)s_exp[s_cur].n);
            fail(m);
        }
        s_cur++;
    }
    s_cs = active;
}

uint8_t Platform_SPI_Xfer(uint8_t v)
{
    if (!s_cs) { fail("byte sent with CS# high"); return 0xFF; }
    if (s_cur >= s_count) return 0xFF;
    const frame_t *f = &s_exp[s_cur];
    size_t i = s_byte++;
    if (i >= f->n) { fail("frame longer than expected"); return 0xFF; }
    if (v != f->tx[i]) {
        char m[80];
        snprintf(m, sizeof m, "byte %u is %02X, expected %02X", (unsigned)i, v, f->tx[i]);
        fail(m);
    }
    return f->rx[i];
}

bool Platform_SPI_TransmitReceive(uint8_t *tx_data, uint8_t *rx_data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        uint8_t r = Platform_SPI_Xfer(tx_data ? tx_data[i] : 0xFF);
        if (rx_data) rx_data[i] = r;
    }
    return true;
}

void Platform_SPI_RegInit(uint32_t br) { (void)br; }
void Platform_SPI_SetSpeed(uint32_t br) { (void)br; }
