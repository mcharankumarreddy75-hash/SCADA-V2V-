// T2 I/O: 3 LEDs, buzzer (via BC547) and button.
// Pins match config.h (S10.2).

#define PIN_LED_RED   PB14   // HIGH = on (330 ohm to GND)
#define PIN_LED_YEL   PB15
#define PIN_LED_GRN   PA8
#define PIN_BUZZER    PB8    // BC547 base via 1k, HIGH = on
#define PIN_BUTTON    PB9    // to GND, internal pull-up, pressed = LOW

static bool buttonRaw = HIGH;
static bool buttonStable = HIGH;
static uint32_t lastEdge = 0;
static bool firstPressSeen = false;

static const uint32_t DEBOUNCE_MS = 30;

static void lamp(uint8_t pin, uint32_t ms) {
  digitalWrite(pin, HIGH);
  delay(ms);
  digitalWrite(pin, LOW);
}

static void beep(uint32_t ms) {
  digitalWrite(PIN_BUZZER, HIGH);
  delay(ms);
  digitalWrite(PIN_BUZZER, LOW);
}

void setup() {
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_YEL, OUTPUT);
  pinMode(PIN_LED_GRN, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_LED_YEL, LOW);
  digitalWrite(PIN_LED_GRN, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  Serial.begin(115200);
  uint32_t t = millis();
  while (!Serial && millis() - t < 3000) {}

  Serial.println("INFO: T2 I/O sequence starts now");
  Serial.println("INFO: watch RED, then YELLOW, then GREEN LED (300 ms each)");
  lamp(PIN_LED_RED, 300);
  lamp(PIN_LED_YEL, 300);
  lamp(PIN_LED_GRN, 300);
  Serial.println("INFO: buzzer beeps twice (200 ms each)");
  beep(200);
  delay(200);
  beep(200);
  Serial.println("INFO: check: 3 LEDs lit in order, 2 beeps heard");
  Serial.println("INFO: now PRESS and RELEASE the button (PB9 to GND)");
  Serial.println("INFO: reset the board to repeat the LED/buzzer sequence");

  buttonRaw = digitalRead(PIN_BUTTON);
  buttonStable = buttonRaw;
  lastEdge = millis();
}

void loop() {
  bool raw = digitalRead(PIN_BUTTON);
  if (raw != buttonRaw) {
    buttonRaw = raw;
    lastEdge = millis();
  }
  if (millis() - lastEdge >= DEBOUNCE_MS && buttonRaw != buttonStable) {
    buttonStable = buttonRaw;
    if (buttonStable == LOW) {
      if (!firstPressSeen) {
        firstPressSeen = true;
        Serial.println("PASS: button press detected");
      }
      Serial.println("INFO: button pressed");
    } else {
      Serial.println("INFO: button released");
    }
  }
}
