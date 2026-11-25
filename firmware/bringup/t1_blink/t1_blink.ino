// T1 Blink: proves the board runs our code (SWD upload works).
// Pins match config.h (S10.2).

#define PIN_LED_BOARD PC13   // onboard LED, active LOW

static uint32_t lastToggle = 0;
static bool ledOn = false;
static uint32_t cycles = 0;
static bool passPrinted = false;

void setup() {
  pinMode(PIN_LED_BOARD, OUTPUT);
  digitalWrite(PIN_LED_BOARD, HIGH);   // LED off

  Serial.begin(115200);
  uint32_t t = millis();
  while (!Serial && millis() - t < 3000) {}

  Serial.println("INFO: T1 blink, PC13 should toggle at 1 Hz");
  lastToggle = millis();
}

void loop() {
  if (millis() - lastToggle >= 500) {
    lastToggle = millis();
    ledOn = !ledOn;
    digitalWrite(PIN_LED_BOARD, ledOn ? LOW : HIGH);

    if (!ledOn) {                       // one full on+off cycle done
      cycles++;
      Serial.print("INFO: blink ");
      Serial.println(cycles);
      if (!passPrinted) {
        passPrinted = true;
        Serial.println("PASS: running (SWD upload worked)");
      }
    }
  }
}
