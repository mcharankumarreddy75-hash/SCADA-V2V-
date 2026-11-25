// T5 GPS: Neo-6M on USART2 (PA3 = GPS TX -> MCU RX, PA2 = MCU TX -> GPS RX).
// Factory settings, no UBX configuration in this test.
// Take it OUTDOORS with the antenna facing the sky.

#include <TinyGPSPlus.h>

// STM32duino core 3.x: the pin constructor lives on class Uart (HardwareSerial is its abstract base).
Uart GPSSerial(PA3, PA2);
TinyGPSPlus gps;

static const uint32_t RAW_WINDOW_MS = 5000;   // raw NMEA shown for 5 s
static const uint32_t STATUS_MS = 1000;       // parsed status once per second

static uint32_t t0 = 0;
static uint32_t lastStatus = 0;
static bool nmeaPassPrinted = false;
static bool fixPrinted = false;
static char line[120];
static uint8_t lineLen = 0;

static void print2(uint8_t v) {
  if (v < 10) Serial.print('0');
  Serial.print(v);
}

static void printStatus(void) {
  Serial.print("INFO: fix=");
  Serial.print(gps.location.isValid() ? "yes" : "no");

  Serial.print(" sats=");
  Serial.print(gps.satellites.isValid() ? gps.satellites.value() : 0);

  Serial.print(" hdop=");
  Serial.print(gps.hdop.isValid() ? gps.hdop.hdop() : 0.0f, 2);

  Serial.print(" lat=");
  Serial.print(gps.location.isValid() ? gps.location.lat() : 0.0, 6);
  Serial.print(" lon=");
  Serial.print(gps.location.isValid() ? gps.location.lng() : 0.0, 6);

  Serial.print(" speed_kmh=");
  Serial.print(gps.speed.isValid() ? gps.speed.kmph() : 0.0, 1);

  Serial.print(" course=");
  Serial.print(gps.course.isValid() ? gps.course.deg() : 0.0, 1);

  Serial.print(" utc=");
  if (gps.time.isValid()) {
    print2(gps.time.hour());
    Serial.print(':');
    print2(gps.time.minute());
    Serial.print(':');
    print2(gps.time.second());
  } else {
    Serial.print("--");
  }

  Serial.print(" chars=");
  Serial.println(gps.charsProcessed());
}

void setup() {
  Serial.begin(115200);
  uint32_t t = millis();
  while (!Serial && millis() - t < 3000) {}

  GPSSerial.begin(9600);
  t0 = millis();
  Serial.println("INFO: T5 GPS. Go outdoors, antenna up. Raw NMEA for 5 s, then status 1 Hz.");
}

void loop() {
  uint32_t now = millis();
  bool rawPhase = (now - t0) < RAW_WINDOW_MS;

  while (GPSSerial.available()) {
    char c = (char)GPSSerial.read();
    gps.encode(c);
    if (rawPhase) {
      if (c == '\n') {
        line[lineLen] = '\0';
        Serial.print("INFO: NMEA ");
        Serial.println(line);
        lineLen = 0;
      } else if (c != '\r' && lineLen < sizeof(line) - 1) {
        line[lineLen++] = c;
      }
    }
  }

  if (!nmeaPassPrinted && gps.passedChecksum() > 0) {
    nmeaPassPrinted = true;
    Serial.println("PASS: NMEA valid");
  }
  if (!fixPrinted && gps.location.isValid()) {
    fixPrinted = true;
    Serial.print("PASS: fix after ");
    Serial.print((now - t0) / 1000);
    Serial.println(" s");
  }
  if (!rawPhase && now - lastStatus >= STATUS_MS) {
    lastStatus = now;
    printStatus();
  }
}
