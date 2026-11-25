// T6 USB: USB CDC (Serial) alive check. Prints a heartbeat every second
// and echoes every character typed on the laptop side.

static uint32_t lastBeat = 0;
static uint32_t beats = 0;

void setup() {
  Serial.begin(115200);
  uint32_t t = millis();
  while (!Serial && millis() - t < 3000) {}

  Serial.println("INFO: T6 USB CDC. Type characters, they are echoed back.");
  lastBeat = millis();
}

void loop() {
  if (millis() - lastBeat >= 1000) {
    lastBeat = millis();
    beats++;
    Serial.print("PASS: USB CDC alive ");
    Serial.println(beats);
  }

  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c < 0) break;
    Serial.print("INFO: echo ");
    if (c >= 0x20 && c < 0x7F) {
      Serial.write((uint8_t)c);
    } else {
      Serial.print("0x");
      Serial.print(c, HEX);
    }
    Serial.print(" (0x");
    Serial.print(c, HEX);
    Serial.println(")");
  }
}
