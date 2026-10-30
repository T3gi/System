/**
 * @file  vision.c
 * @brief "Hand covers the lens" detector.  See vision.h.
 */
#include "vision.h"

static bool     s_armed;
static uint32_t s_dark_frames;

uint8_t Vision_MeanLuma(const uint8_t *uyvy, uint32_t bytes)
{
    uint32_t sum = 0;
    uint32_t n = 0;

    for (uint32_t i = 1; i < bytes; i += 2) {
        sum += uyvy[i];
        n++;
    }
    if (n == 0) {
        return 0;
    }
    return (uint8_t)(sum / n);
}

void Vision_Reset(void)
{
    s_armed = true;
    s_dark_frames = 0;
}

bool Vision_Update(uint8_t luma)
{
    if (!s_armed) {
        /* Already fired: wait until the hand is taken away. */
        if (luma >= VISION_LIGHT_LEVEL) {
            Vision_Reset();
        }
        return false;
    }
    if (luma >= VISION_DARK_LEVEL) {
        s_dark_frames = 0;
        return false;
    }
    s_dark_frames++;
    if (s_dark_frames < VISION_DARK_FRAMES) {
        return false;
    }
    s_armed = false;
    return true;
}
