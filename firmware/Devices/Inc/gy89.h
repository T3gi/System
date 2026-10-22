#ifndef GY89_H
#define GY89_H

#include <stdbool.h>

#define LSM303D_ADDR   (0x1D << 1)
#define L3GD20_ADDR    (0x6B << 1)
#define BMP180_ADDR    (0x77 << 1)

/* Expected ID register values (datasheets of the three chips). */
#define LSM303D_ID     0x49
#define L3GD20_ID      0xD4
#define BMP180_ID      0x55

/* Read and print the ID registers of the three GY-89 chips (LSM303D, L3GD20,
 * BMP180).  Returns true only if every chip answered with its expected ID. */
bool GY89_ReadIDs(void);

#endif
