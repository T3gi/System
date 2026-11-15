/**
 * @file  stm32f4xx_hal_timebase_tim.c
 * @brief HAL time base on TIM5 (LR4).
 *
 * FreeRTOS owns SysTick (port.c programs it for configTICK_RATE_HZ), so
 * HAL_GetTick()/HAL_Delay() and the HAL driver timeouts run from TIM5 at
 * 1 kHz instead.  This keeps HAL time correct when the kernel tick is changed
 * (scenario "tick rate", 100 Hz) and before the scheduler starts (boot
 * self-test).  This is what CubeMX calls "Timebase Source = TIM5".
 *
 * TIM5 is unused elsewhere (TIM1 = camera XCLK, TIM3 = servo PWM).
 */
#include "stm32f4xx_hal.h"

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    /* Called from HAL_Init() (HSI 16 MHz) and again from HAL_RCC_ClockConfig(). */
    __HAL_RCC_TIM5_CLK_ENABLE();

    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t timclk = pclk1;
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_HCLK_DIV1) {
        timclk = 2u * pclk1;           /* APB1 timers run at twice PCLK1 when divided */
    }

    TIM5->CR1  = 0;
    TIM5->PSC  = (timclk / 1000000u) - 1u;   /* 1 MHz */
    TIM5->ARR  = (1000000u / (1000u / uwTickFreq)) - 1u;   /* 1 kHz for HAL_TICK_FREQ_1KHZ */
    TIM5->EGR  = TIM_EGR_UG;
    TIM5->SR   = 0;
    TIM5->DIER = TIM_DIER_UIE;

    if (TickPriority >= (1UL << __NVIC_PRIO_BITS)) {
        return HAL_ERROR;
    }
    HAL_NVIC_SetPriority(TIM5_IRQn, TickPriority, 0U);
    HAL_NVIC_EnableIRQ(TIM5_IRQn);
    uwTickPrio = TickPriority;

    TIM5->CR1 = TIM_CR1_CEN;
    return HAL_OK;
}

void HAL_SuspendTick(void)
{
    TIM5->DIER &= ~TIM_DIER_UIE;
}

void HAL_ResumeTick(void)
{
    TIM5->DIER |= TIM_DIER_UIE;
}

void TIM5_IRQHandler(void)
{
    if (TIM5->SR & TIM_SR_UIF) {
        TIM5->SR = ~TIM_SR_UIF;
        HAL_IncTick();
    }
}
