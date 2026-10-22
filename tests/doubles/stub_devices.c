#include "stub_devices.h"
#include "bus_log.h"
#include "i2c_scan.h"
#include "servo.h"
#include "w25q64.h"
#include "mpu6050.h"
#include "gy89.h"
#include "ov7670_capture.h"

bool stub_w25q64_ok;
bool stub_mpu6050_ok;
bool stub_gy89_ok;
bool stub_ov7670_ok;

void stub_devices_reset(void)
{
    stub_w25q64_ok = stub_mpu6050_ok = stub_gy89_ok = stub_ov7670_ok = true;
}

void I2C_Scan(void)                 { bus_log("SCAN;"); }
void Servo_Init(void)               { bus_log("SERVO INIT;"); }
void Servo_SetAngle(uint8_t angle)  { bus_log("SERVO %u;", (unsigned)angle); }
bool W25Q64_ReadID(void)            { bus_log("W25Q64 ID;"); return stub_w25q64_ok; }
bool MPU6050_Init(void)             { bus_log("MPU6050 INIT;"); return stub_mpu6050_ok; }
bool GY89_ReadIDs(void)             { bus_log("GY89 ID;"); return stub_gy89_ok; }
void OV7670_PowerUp(void)           { bus_log("CAM POWERUP;"); }
bool OV7670_Probe(void)             { bus_log("CAM PROBE;"); return stub_ov7670_ok; }
