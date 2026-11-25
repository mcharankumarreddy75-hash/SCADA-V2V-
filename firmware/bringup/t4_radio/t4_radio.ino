// T4 Radio: SX1281 (E28-2G4M20SX) packet exchange, settings per S7.1.
// NODE 1 sends "hello <n>" every 500 ms; NODE 2 answers "ack <n>".
// Build NODE 2 without editing this file:
//   --build-property "build.extra_flags=-DNODE=2"

#ifndef NODE
#define NODE 1            // 1 = OBU #1, 2 = OBU #2
#endif
#if (NODE != 1) && (NODE != 2)
#error "NODE must be 1 or 2"
#endif

#include <RadioLib.h>
#include <string.h>
#include <stdlib.h>

// Pins match config.h (S10.2)
#define PIN_RF_NSS    PA4
#define PIN_RF_DIO1   PB1
#define PIN_RF_NRST   PA1
#define PIN_RF_BUSY   PB0
#define PIN_RF_TXEN   PB12
#define PIN_RF_RXEN   PB13

// Radio settings (S7.1)
#define RF_FREQ_HZ       2445000000UL  // 2445.0 MHz
#define RF_BW_HZ         812500UL      // RadioLib LoRa bandwidth is in Hz
#define RF_SF            7
#define RF_CR            5             // RadioLib: 5 = 4/5 (LoRa CR 4..8 = 4/4..4/8)
#define RF_PREAMBLE      12
#define RF_POWER_DBM     0             // VERIFY max chip output for E28 PA (S7.1)
#define RF_PAYLOAD_LEN   32
#define TX_PERIOD_MS     500

Module rfModule(PIN_RF_NSS, PIN_RF_DIO1, PIN_RF_NRST, PIN_RF_BUSY);
SX1281 radio(&rfModule);

static volatile bool rfIrq = false;
static void rfIsr(void) { rfIrq = true; }   // DIO1: only set a flag

static bool radioOk = false;
#if NODE == 1
static uint32_t counter = 0;      // NODE 1 only: hello counter
#endif
static uint32_t lastTx = 0;
static uint32_t lastFailMsg = 0;
static char txBuf[RF_PAYLOAD_LEN];
static uint8_t rxBuf[RF_PAYLOAD_LEN];
static char rxText[RF_PAYLOAD_LEN + 1];
static bool rxValid = false;

static void transmitText(const char *text) {
  memset(txBuf, ' ', RF_PAYLOAD_LEN);
  size_t l = strlen(text);
  if (l > RF_PAYLOAD_LEN) l = RF_PAYLOAD_LEN;
  memcpy(txBuf, text, l);

  int16_t st = radio.transmit((uint8_t *)txBuf, RF_PAYLOAD_LEN);
  rfIrq = false;                     // TX-done also pulses DIO1; not a packet
  radio.startReceive();              // back to listening

  if (st == RADIOLIB_ERR_NONE) {
    Serial.print("INFO: TX \"");
    Serial.print(text);
    Serial.println("\" (32 B)");
  } else {
    Serial.print("FAIL: TX code=");
    Serial.println(st);
  }
}

static void handleRx(void) {
  rfIrq = false;
  rxValid = false;

  size_t len = radio.getPacketLength();
  if (len > RF_PAYLOAD_LEN) len = RF_PAYLOAD_LEN;
  int16_t st = (len > 0) ? radio.readData(rxBuf, len) : RADIOLIB_ERR_CRC_MISMATCH;
  if (st != RADIOLIB_ERR_NONE) {
    Serial.print("INFO: RX error code=");
    Serial.println(st);
    radio.startReceive();
    return;
  }

  memcpy(rxText, rxBuf, len);
  rxText[len] = '\0';
  while (len > 0 && rxText[len - 1] == ' ') rxText[--len] = '\0';

  Serial.print("INFO: RX \"");
  Serial.print(rxText);
  Serial.print("\" RSSI=");
  Serial.print(radio.getRSSI(), 1);
  Serial.print(" dBm SNR=");
  Serial.print(radio.getSNR(), 1);
  Serial.println(" dB");

  rxValid = true;
  radio.startReceive();
}

void setup() {
  Serial.begin(115200);
  uint32_t t = millis();
  while (!Serial && millis() - t < 3000) {}

  Serial.print("INFO: T4 radio, NODE ");
  Serial.println(NODE);

  radio.setRfSwitchPins(PIN_RF_RXEN, PIN_RF_TXEN);

  ConfigLoRa_t cfg;
  cfg.frequency = RF_FREQ_HZ;
  cfg.bandwidth = RF_BW_HZ;
  cfg.spreadingFactor = RF_SF;
  cfg.codingRate = RF_CR;
  cfg.syncWord = RADIOLIB_SX128X_SYNC_WORD_PRIVATE;
  cfg.power = RF_POWER_DBM;
  cfg.preambleLength = RF_PREAMBLE;

  int16_t st = radio.begin(cfg);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.print("FAIL: radio init code=");
    Serial.println(st);
    return;
  }

  radio.setDio1Action(rfIsr);
  radio.startReceive();
  radioOk = true;

  Serial.println("PASS: radio init");
  RadioLibTime_t toa = radio.getTimeOnAir(RF_PAYLOAD_LEN);   // microseconds
  Serial.print("INFO: time-on-air 32 B = ");
  Serial.print((unsigned long)(toa / 1000));
  Serial.println(" ms (expect ~12 ms)");
  lastTx = millis();
}

void loop() {
  if (!radioOk) {
    if (millis() - lastFailMsg >= 2000) {
      lastFailMsg = millis();
      Serial.println("FAIL: radio not initialised, check wiring and power");
    }
    return;
  }

  if (rfIrq) {
    handleRx();
    if (rxValid) {
#if NODE == 2
      if (strncmp(rxText, "hello", 5) == 0) {
        const char *p = strchr(rxText, ' ');
        unsigned long n = p ? strtoul(p + 1, NULL, 10) : 0;
        char reply[24];
        snprintf(reply, sizeof(reply), "ack %lu", n);
        transmitText(reply);
      }
#else
      if (strncmp(rxText, "ack", 3) == 0) {
        const char *p = strchr(rxText, ' ');
        unsigned long n = p ? strtoul(p + 1, NULL, 10) : 0;
        if (n == (unsigned long)counter) {
          Serial.print("PASS: round trip ok, hello ");
          Serial.print(n);
          Serial.println(" acked");
        }
      }
#endif
    }
  }

#if NODE == 1
  if (millis() - lastTx >= TX_PERIOD_MS) {
    lastTx = millis();
    counter++;
    char msg[24];
    snprintf(msg, sizeof(msg), "hello %lu", (unsigned long)counter);
    transmitText(msg);
  }
#endif
}
