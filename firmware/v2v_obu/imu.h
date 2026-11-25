#pragma once
// MPU-6050 driver (I2C1), spec S10.4 / S9.1 / S9.6 / S13.2.
#include <Arduino.h>
#include <stdint.h>

typedef struct {
    bool   ok;
    double ax, ay, az;          // calibrated, axis-mapped (forward, left, up), m/s^2
    double gx, gy, gz;          // calibrated, axis-mapped, deg/s
    double a_long;              // low-pass of ax (alpha IMU_ALONG_ALPHA)
    double roll, pitch;         // complementary filter, deg
    double az_lp, az_hp;        // az low-pass / high-pass (pothole)
    double a_total_g;           // |a| / g (includes gravity)
} imu_state_t;

bool imu_init();          // WHO_AM_I check + register setup (boot)
void imu_calibrate();     // S13.2: IMU_CAL_TIME_MS still, boot only. Offsets are applied in MAPPED axes.
bool imu_update();        // call at 100 Hz: burst read, map, filter. false if the read failed.
const imu_state_t *imu_get();
