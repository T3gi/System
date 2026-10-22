#include "fake_w25q64.h"
#include "platform_spi.h"
#include "bus_log.h"
#include <string.h>

#define SR_BUSY 0x01u
#define SR_WEL  0x02u
#define LOG_BYTES 8u

fake_w25_line_t fake_w25_line;
uint8_t fake_w25_jedec[3];
int  fake_w25_busy_polls;
bool fake_w25_busy_forever;
bool fake_w25_write_protect;

static uint8_t  s_mem[FAKE_W25_SIZE];
static bool     s_cs;
static uint8_t  s_frame[300];
static uint32_t s_n;              /* bytes clocked in the current frame */
static bool     s_wel;
static int      s_busy_left;      /* -1 = forever */
static uint32_t s_br;

void fake_w25_reset(void)
{
    memset(s_mem, 0xFF, sizeof s_mem);
    fake_w25_line = FAKE_W25_OK;
    fake_w25_jedec[0] = 0xEF; fake_w25_jedec[1] = 0x40; fake_w25_jedec[2] = 0x17;
    fake_w25_busy_polls = 3;
    fake_w25_busy_forever = false;
    fake_w25_write_protect = false;
    s_cs = false;
    s_n = 0;
    s_wel = false;
    s_busy_left = 0;
    s_br = 0;
}

uint8_t *fake_w25_mem(void) { return s_mem; }
uint32_t fake_w25_last_br(void) { return s_br; }

static uint8_t status(void) { return (uint8_t)((s_wel ? SR_WEL : 0) | (s_busy_left ? SR_BUSY : 0)); }

static uint8_t read_mem(uint32_t a) { return s_mem[a % FAKE_W25_SIZE]; }

static uint32_t frame_addr(void)
{
    return ((uint32_t)s_frame[1] << 16) | ((uint32_t)s_frame[2] << 8) | s_frame[3];
}

static void start_busy(void) { s_busy_left = fake_w25_busy_forever ? -1 : fake_w25_busy_polls; }

/* Called at the rising edge of CS#: the chip acts on the completed command. */
static void frame_end(void)
{
    bus_log("SPI");
    for (uint32_t i = 0; i < s_n && i < LOG_BYTES; i++) bus_log(" %02X", s_frame[i]);
    if (s_n > LOG_BYTES) bus_log(" +%u", (unsigned)(s_n - LOG_BYTES));
    bus_log(";");

    if (s_n == 0 || s_busy_left) return;        /* a busy chip ignores everything but 05 */
    switch (s_frame[0]) {
    case 0x06: if (!fake_w25_write_protect) s_wel = true; break;
    case 0x04: s_wel = false; break;
    case 0x20:
    case 0xD8:
        if (s_n == 4 && s_wel) {
            uint32_t size = (s_frame[0] == 0x20) ? 4096u : 65536u;
            uint32_t a = frame_addr() & ~(size - 1u);
            memset(s_mem + a, 0xFF, size);
            start_busy();
        }
        break;
    case 0x02:
        if (s_n > 4 && s_wel) {
            uint32_t a = frame_addr();
            for (uint32_t i = 0; i < s_n - 4; i++) {
                /* The page wraps instead of continuing into the next one. */
                uint32_t p = (a & ~0xFFu) | ((a + i) & 0xFFu);
                s_mem[p] &= s_frame[4 + i];       /* NOR: programming only clears bits */
            }
            start_busy();
        }
        break;
    default: break;
    }
}

void Platform_SPI_SetCS(bool active)
{
    if (s_cs && !active) frame_end();
    if (active && !s_cs) s_n = 0;
    s_cs = active;
}

uint8_t Platform_SPI_Xfer(uint8_t v)
{
    uint32_t i = s_n;
    if (s_n < sizeof s_frame) s_frame[s_n] = v;
    s_n++;
    if (fake_w25_line == FAKE_W25_ABSENT) return 0xFF;
    if (!s_cs || i == 0) return 0xFF;           /* DO is high-Z while the command shifts in */

    switch (s_frame[0]) {
    case 0x9F:
        return (i <= 3) ? fake_w25_jedec[i - 1] : 0xFF;
    case 0x05: {
        uint8_t s = status();
        if (s_busy_left > 0 && --s_busy_left == 0) s_wel = false;   /* operation done */
        return s;
    }
    case 0x03:
        return (i >= 4) ? read_mem(frame_addr() + i - 4) : 0xFF;
    case 0x0B:
        return (i >= 5) ? read_mem(frame_addr() + i - 5) : 0xFF;
    default:
        return 0xFF;
    }
}

bool Platform_SPI_TransmitReceive(uint8_t *tx_data, uint8_t *rx_data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        uint8_t r = Platform_SPI_Xfer(tx_data ? tx_data[i] : 0xFF);
        if (rx_data) rx_data[i] = r;
    }
    return true;
}

void Platform_SPI_RegInit(uint32_t br)
{
    bus_log("SPIINIT %u;", (unsigned)br);
    s_br = br;
}

void Platform_SPI_SetSpeed(uint32_t br)
{
    bus_log("SPISPEED %u;", (unsigned)br);
    s_br = br;
}
