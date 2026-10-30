#include "platform_board.h"
#include "main.h"

extern UART_HandleTypeDef huart2;

/* RX diagnostics — surfaced by PING so a flaky link is visible as a number
 * rather than inferred from missing replies. */
static volatile uint32_t s_rx_bytes    = 0;
static volatile uint32_t s_rx_overruns = 0;
static volatile uint32_t s_rx_dropped  = 0;

/* Ring buffer for received bytes.  The interrupt writes at s_rx_head, the
 * main loop reads at s_rx_tail.  Each index is changed by one side only, so
 * the two never fight over the same variable. */
#define RX_RING_SIZE 256
static volatile uint8_t  s_rx_ring[RX_RING_SIZE];
static volatile uint16_t s_rx_head = 0;
static volatile uint16_t s_rx_tail = 0;

/* --- Retarget printf() --------------------------------------------------- */
int _write(int file, char *ptr, int len)
{
    (void)file;
    HAL_UART_Transmit(&huart2, (uint8_t *)ptr, (uint16_t)len, HAL_MAX_DELAY);
    return len;
}

int __io_putchar(int ch)
{
    uint8_t b = (uint8_t)ch;
    HAL_UART_Transmit(&huart2, &b, 1, HAL_MAX_DELAY);
    return ch;
}

void Platform_Console_Start(void)
{
    /* Raise USART2_IRQn every time a byte arrives. */
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_RXNE);
}

/* Called from USART2_IRQHandler for every received byte. */
void Platform_Console_IrqHandler(void)
{
    uint32_t sr = huart2.Instance->SR;
    if (sr & (USART_SR_ORE | USART_SR_RXNE)) {
        /* Reading DR clears both RXNE and ORE.  Crucially the byte in DR is
         * still valid data (the overrun destroyed an *earlier* byte), so it
         * must be kept. */
        uint8_t d = (uint8_t)(huart2.Instance->DR & 0xFFu);
        if (sr & USART_SR_ORE) s_rx_overruns++;
        s_rx_bytes++;

        uint16_t next = s_rx_head + 1;
        if (next == RX_RING_SIZE) {
            next = 0;
        }
        if (next == s_rx_tail) {
            s_rx_dropped++;          /* buffer full: the main loop is too slow */
        } else {
            s_rx_ring[s_rx_head] = d;
            s_rx_head = next;
        }
    }
}

int Platform_Console_GetChar(void)
{
    if (s_rx_tail == s_rx_head) {
        return -1;                   /* nothing received */
    }
    int d = s_rx_ring[s_rx_tail];
    uint16_t next = s_rx_tail + 1;
    if (next == RX_RING_SIZE) {
        next = 0;
    }
    s_rx_tail = next;
    return d;
}

uint32_t Platform_Console_RxBytes(void)    { return s_rx_bytes; }
uint32_t Platform_Console_RxOverruns(void) { return s_rx_overruns; }
uint32_t Platform_Console_RxDropped(void)  { return s_rx_dropped; }

bool Platform_ButtonPressed(void)
{
    return HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_RESET;
}

void Platform_LedToggle(void)
{
    HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
}

void Platform_LedSet(bool on)
{
    if (on) {
        HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
    }
}

void Platform_SystemReset(void)
{
    while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC) == RESET) {}
    NVIC_SystemReset();
}
