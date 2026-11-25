// T7 Battery: ADC on PA0, divider battery+ -> 100k -> PA0 -> 100k -> GND (S6.6).
// Vbat = raw / 4095 * 3.3 * 2 ; pct = clamp((Vbat - 3.30) / (4.20 - 3.30) * 100, 0, 100)

#define PIN_VBAT PA0

static uint32_t lastPrint = 0;

void setup() {
  analogReadResolution(12);
  Serial.begin(115200);
  uint32_t t = millis();
  while (!Serial && millis() - t < 3000) {}

  Serial.println("INFO: T7 battery, one reading per second");
  lastPrint = millis() - 1000;
}

void loop() {
  if (millis() - lastPrint < 1000) return;
  lastPrint = millis();

  uint16_t raw = analogRead(PIN_VBAT);
  float vbat = raw / 4095.0f * 3.3f * 2.0f;
  float pct = (vbat - 3.30f) / (4.20f - 3.30f) * 100.0f;
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;

  Serial.print("INFO: raw=");
  Serial.print(raw);
  Serial.print(" Vbat=");
  Serial.print(vbat, 3);
  Serial.print(" V pct=");
  Serial.print(pct, 1);
  Serial.println(" %");
  Serial.println("INFO: compare with multimeter, must be within +/-0.05 V");
}
