// T3 IMU: MPU-6050 on I2C1 (PB6 SCL, PB7 SDA), register setup per S10.4.
// Pass: WHO_AM_I = 0x68; flat and still gives az ~ +9.8, ax/ay ~ 0.

#include <Wire.h>

#define PIN_I2C_SCL PB6
#define PIN_I2C_SDA PB7

#define MPU_ADDR        0x68
#define REG_PWR_MGMT_1  0x6B
#define REG_SMPLRT_DIV  0x19
#define REG_CONFIG      0x1A
#define REG_GYRO_CONFIG 0x1B
#define REG_ACCEL_CONFIG 0x1C
#define REG_ACCEL_XOUT  0x3B
#define REG_WHO_AM_I    0x75

static const uint32_t SAMPLE_MS = 200;     // 5 per second
static const uint32_t VERDICT_MS = 2000;   // verdict after 2 s of samples

static bool imuOk = false;
static uint32_t startMs = 0;
static uint32_t lastSample = 0;
static bool verdictDone = false;
static float sumAx = 0, sumAy = 0, sumAz = 0;
static uint32_t nSamples = 0;

static bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission((uint8_t)MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool readRegs(uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission((uint8_t)MPU_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = (uint8_t)Wire.read();
  return true;
}

void setup() {
  Serial.begin(115200);
  uint32_t t = millis();
  while (!Serial && millis() - t < 3000) {}

  Wire.setSDA(PIN_I2C_SDA);
  Wire.setSCL(PIN_I2C_SCL);
  Wire.begin();
  Wire.setClock(400000);

  uint8_t who = 0;
  if (!readRegs(REG_WHO_AM_I, &who, 1)) {
    Serial.println("FAIL: WHO_AM_I read error (check wiring, AD0 to GND, 3.3 V)");
    return;
  }
  if (who != 0x68) {
    Serial.print("FAIL: WHO_AM_I=0x");
    Serial.print(who, HEX);
    Serial.println(" expected 0x68");
    return;
  }
  Serial.println("PASS: WHO_AM_I = 0x68");

  bool ok = true;
  ok &= writeReg(REG_PWR_MGMT_1, 0x01);    // wake, gyro PLL clock
  delay(100);
  ok &= writeReg(REG_SMPLRT_DIV, 0x09);    // 100 Hz
  ok &= writeReg(REG_CONFIG, 0x03);        // DLPF ~44 Hz
  ok &= writeReg(REG_GYRO_CONFIG, 0x08);   // +/-500 deg/s
  ok &= writeReg(REG_ACCEL_CONFIG, 0x10);  // +/-8 g
  if (!ok) {
    Serial.println("FAIL: register write error");
    return;
  }
  imuOk = true;
  startMs = millis();
  lastSample = startMs - SAMPLE_MS;
  Serial.println("INFO: keep the board FLAT and STILL for 2 s");
}

void loop() {
  if (!imuOk) return;
  uint32_t now = millis();
  if (now - lastSample < SAMPLE_MS) return;
  lastSample = now;

  uint8_t b[14];
  if (!readRegs(REG_ACCEL_XOUT, b, 14)) {
    Serial.println("FAIL: burst read error");
    return;
  }
  int16_t rawAx = (int16_t)((b[0] << 8) | b[1]);
  int16_t rawAy = (int16_t)((b[2] << 8) | b[3]);
  int16_t rawAz = (int16_t)((b[4] << 8) | b[5]);
  int16_t rawGx = (int16_t)((b[8] << 8) | b[9]);
  int16_t rawGy = (int16_t)((b[10] << 8) | b[11]);
  int16_t rawGz = (int16_t)((b[12] << 8) | b[13]);

  float ax = rawAx / 4096.0f * 9.81f;
  float ay = rawAy / 4096.0f * 9.81f;
  float az = rawAz / 4096.0f * 9.81f;
  float gx = rawGx / 65.5f;
  float gy = rawGy / 65.5f;
  float gz = rawGz / 65.5f;

  Serial.print("INFO: ax=");  Serial.print(ax, 2);
  Serial.print(" ay=");       Serial.print(ay, 2);
  Serial.print(" az=");       Serial.print(az, 2);
  Serial.print(" m/s2  gx="); Serial.print(gx, 2);
  Serial.print(" gy=");       Serial.print(gy, 2);
  Serial.print(" gz=");       Serial.print(gz, 2);
  Serial.println(" deg/s");

  if (!verdictDone) {
    sumAx += ax; sumAy += ay; sumAz += az;
    nSamples++;
    if (now - startMs >= VERDICT_MS) {
      verdictDone = true;
      float mAx = sumAx / nSamples;
      float mAy = sumAy / nSamples;
      float mAz = sumAz / nSamples;
      bool pass = (mAz >= 9.0f && mAz <= 10.6f) &&
                  (fabsf(mAx) < 1.0f) && (fabsf(mAy) < 1.0f);
      Serial.print(pass ? "PASS" : "FAIL");
      Serial.print(": flat-still mean ax=");  Serial.print(mAx, 2);
      Serial.print(" ay=");                    Serial.print(mAy, 2);
      Serial.print(" az=");                    Serial.print(mAz, 2);
      Serial.println(pass ? " (expect az ~9.8, ax/ay ~0)"
                          : " (expected az 9.0..10.6, |ax|,|ay| < 1.0)");
    }
  }
}
