#ifndef GY89_H
#define GY89_H

#define LSM303D_ADDR   (0x1D << 1)
#define L3GD20_ADDR    (0x6B << 1)
#define BMP180_ADDR    (0x77 << 1)

/* Print the ID registers of the three GY-89 chips (LSM303D, L3GD20, BMP180). */
void GY89_ReadIDs(void);

#endif
