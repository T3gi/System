/**
 * @file  vision.h
 * @brief Visual trigger of FR-05: "hand covers the lens".  The frame's mean
 *        brightness must stay below VISION_DARK_LEVEL for VISION_DARK_FRAMES
 *        frames in a row; the trigger then fires once and re-arms only after
 *        the picture is bright again (>= VISION_LIGHT_LEVEL), so a hand held
 *        on the lens does not keep toggling the damper.  Pure logic.
 */
#ifndef VISION_H
#define VISION_H

#include <stdbool.h>
#include <stdint.h>

/* Luma levels 0..255.  Starting values; tune on the board (TICK shows lum=). */
#define VISION_DARK_LEVEL   40u
#define VISION_LIGHT_LEVEL  70u
#define VISION_DARK_FRAMES  2u

/** Mean luma of a UYVY frame (Y is every second byte, starting at index 1). */
uint8_t Vision_MeanLuma(const uint8_t *uyvy, uint32_t bytes);

/** Armed, no dark frames counted. */
void Vision_Reset(void);

/** Feed one frame's mean luma.  @return true once per covering of the lens. */
bool Vision_Update(uint8_t luma);

#endif /* VISION_H */
