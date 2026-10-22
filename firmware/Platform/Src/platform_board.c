#include "platform_board.h"
#include "main.h"

extern UART_HandleTypeDef huart2;

/* RX diagnostics — surfaced by PING so a flaky link is visible as a number
 * rather than inferred from missing replies. */
static volatile uint32_t s_rx_bytes    = 0;
static volatile uint32_t s_rx_overruns = 0;

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

int Platform_Console_GetChar(void)
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

uint32_t Platform_Console_RxBytes(void)    { return s_rx_bytes; }
uint32_t Platform_Console_RxOverruns(void) { return s_rx_overruns; }

bool Platform_ButtonPressed(void)
{
    return HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_RESET;
}

void Platform_LedToggle(void)
{
    HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
}

void Platform_SystemReset(void)
{
    while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC) == RESET) {}
    NVIC_SystemReset();
}
