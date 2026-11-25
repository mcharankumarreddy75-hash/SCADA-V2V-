#include <Wire.h>
#include <math.h>
#include "config.h"
#include "imu.h"

static const struct { uint8_t idx; int8_t sign; } AXIS_MAP[3] = IMU_AXIS_MAP;

static imu_state_t s;
static double off_a[3];      // accel offsets in mapped axes (m/s^2)
static double off_g[3];      // gyro offsets in mapped axes (deg/s)
static bool seeded = false;

static bool write_reg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(IMU_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static bool read_reg(uint8_t reg, uint8_t *val) {
    Wire.beginTransmission(IMU_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)IMU_ADDR, (uint8_t)1) != 1) return false;
    *val = Wire.read();
    return true;
}

// Burst-reads 14 bytes from ACCEL_XOUT_H: AX AY AZ TEMP GX GY GZ (big-endian int16), raw sensor axes.
static bool read_raw(int16_t acc[3], int16_t gyr[3]) {
    Wire.beginTransmission(IMU_ADDR);
    Wire.write(IMU_REG_ACCEL_XOUT_H);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)IMU_ADDR, (uint8_t)IMU_BURST_LEN) != IMU_BURST_LEN) {
        while (Wire.available()) Wire.read();
        return false;
    }
    uint8_t b[IMU_BURST_LEN];
    for (uint8_t i = 0; i < IMU_BURST_LEN; i++) b[i] = Wire.read();
    acc[0] = (int16_t)((b[0] << 8) | b[1]);
    acc[1] = (int16_t)((b[2] << 8) | b[3]);
    acc[2] = (int16_t)((b[4] << 8) | b[5]);
    gyr[0] = (int16_t)((b[8] << 8) | b[9]);
    gyr[1] = (int16_t)((b[10] << 8) | b[11]);
    gyr[2] = (int16_t)((b[12] << 8) | b[13]);
    return true;
}

// Applies IMU_AXIS_MAP: output k = sign * raw[idx] for forward, left, up; converts units.
static void map_scale(const int16_t acc[3], const int16_t gyr[3], double a[3], double g[3]) {
    for (int k = 0; k < 3; k++) {
        a[k] = AXIS_MAP[k].sign * (acc[AXIS_MAP[k].idx] / IMU_ACC_LSB_PER_G) * GRAVITY_MS2;
        g[k] = AXIS_MAP[k].sign * (gyr[AXIS_MAP[k].idx] / IMU_GYR_LSB_PER_DPS);
    }
}

bool imu_init() {
    s.ok = false;
    Wire.setSCL(PIN_I2C_SCL);
    Wire.setSDA(PIN_I2C_SDA);
    Wire.begin();
    Wire.setClock(IMU_I2C_CLOCK_HZ);

    uint8_t who = 0;
    if (!read_reg(IMU_REG_WHO_AM_I, &who) || who != IMU_WHO_AM_I_VALUE) return false;
    if (!write_reg(IMU_REG_PWR_MGMT_1, IMU_VAL_PWR_MGMT_1)) return false;
    delay(IMU_WAKE_DELAY_MS);
    if (!write_reg(IMU_REG_SMPLRT_DIV, IMU_VAL_SMPLRT_DIV)) return false;
    if (!write_reg(IMU_REG_CONFIG, IMU_VAL_CONFIG)) return false;
    if (!write_reg(IMU_REG_GYRO_CONFIG, IMU_VAL_GYRO_CONFIG)) return false;
    if (!write_reg(IMU_REG_ACCEL_CONFIG, IMU_VAL_ACCEL_CONFIG)) return false;
    s.ok = true;
    seeded = false;
    return true;
}

void imu_calibrate() {
    // Average the mapped, uncorrected readings over the still period (boot only).
    for (int k = 0; k < 3; k++) { off_a[k] = 0; off_g[k] = 0; }
    double sa[3] = {0, 0, 0};
    double sg[3] = {0, 0, 0};
    uint16_t n = 0;
    const uint32_t step_ms = IMU_CAL_TIME_MS / IMU_CAL_SAMPLES;
    for (uint16_t i = 0; i < IMU_CAL_SAMPLES; i++) {
        int16_t acc[3], gyr[3];
        double a[3], g[3];
        if (read_raw(acc, gyr)) {
            map_scale(acc, gyr, a, g);
            for (int k = 0; k < 3; k++) { sa[k] += a[k]; sg[k] += g[k]; }
            n++;
        }
        delay(step_ms);
    }
    if (n == 0) return;
    for (int k = 0; k < 3; k++) {
        off_a[k] = sa[k] / n;
        off_g[k] = sg[k] / n;
    }
    off_a[2] -= GRAVITY_MS2;     // up axis reads +1 g when level
}

bool imu_update() {
    int16_t acc[3], gyr[3];
    if (!s.ok || !read_raw(acc, gyr)) {
        s.ok = false;
        return false;
    }
    s.ok = true;
    double a[3], g[3];
    map_scale(acc, gyr, a, g);
    s.ax = a[0] - off_a[0];
    s.ay = a[1] - off_a[1];
    s.az = a[2] - off_a[2];
    s.gx = g[0] - off_g[0];
    s.gy = g[1] - off_g[1];
    s.gz = g[2] - off_g[2];

    double roll_acc = atan2(s.ay, s.az) * IMU_RAD2DEG;
    double pitch_acc = atan2(-s.ax, sqrt(s.ay * s.ay + s.az * s.az)) * IMU_RAD2DEG;

    if (!seeded) {
        seeded = true;
        s.a_long = s.ax;
        s.az_lp = s.az;
        s.roll = roll_acc;
        s.pitch = pitch_acc;
    } else {
        s.a_long += IMU_ALONG_ALPHA * (s.ax - s.a_long);
        s.az_lp += IMU_AZ_LP_ALPHA * (s.az - s.az_lp);
        s.roll = IMU_CF_ALPHA * (s.roll + s.gx * IMU_DT_S) + (1.0 - IMU_CF_ALPHA) * roll_acc;
        s.pitch = IMU_CF_ALPHA * (s.pitch + s.gy * IMU_DT_S) + (1.0 - IMU_CF_ALPHA) * pitch_acc;
    }
    s.az_hp = s.az - s.az_lp;
    s.a_total_g = sqrt(s.ax * s.ax + s.ay * s.ay + s.az * s.az) / GRAVITY_MS2;
    return true;
}

const imu_state_t *imu_get() {
    return &s;
}
