#include <TinyGPSPlus.h>
#include <string.h>
#include <math.h>
#include "config.h"
#include "v2v_packet.h"
#include "gps.h"

Uart GPSSerial(PIN_GPS_RX, PIN_GPS_TX);   // USART2; STM32duino 3.1.0 has no HardwareSerial ctor (abstract base)
static TinyGPSPlus tiny;
static gps_state_t g;

void ubx_checksum(const uint8_t *buf, size_t len, uint8_t *ck_a, uint8_t *ck_b) {
    uint8_t a = 0, b = 0;
    for (size_t i = 0; i < len; i++) { a += buf[i]; b += a; }
    *ck_a = a;
    *ck_b = b;
}

// Sends one UBX frame. Boot only (flush + short pause).
static void ubx_send(uint8_t cls, uint8_t id, const uint8_t *payload, uint16_t plen) {
    uint8_t body[4 + GPS_UBX_MAX_PAYLOAD];           // class, id, len LE16, payload
    body[0] = cls;
    body[1] = id;
    body[2] = (uint8_t)(plen & 0xFF);
    body[3] = (uint8_t)(plen >> 8);
    memcpy(&body[4], payload, plen);
    uint8_t ck_a, ck_b;
    ubx_checksum(body, 4 + plen, &ck_a, &ck_b);
    GPSSerial.write(UBX_SYNC1);
    GPSSerial.write(UBX_SYNC2);
    GPSSerial.write(body, 4 + plen);
    GPSSerial.write(ck_a);
    GPSSerial.write(ck_b);
    GPSSerial.flush();
    delay(GPS_UBX_GAP_MS);
}

#define SEND_UBX(cls, id, arr) do { static const uint8_t p_[] = arr; \
    ubx_send(cls, id, p_, sizeof(p_)); } while (0)

bool gps_configure() {
    // Step 1: disable GSV, GSA, GLL, VTG
    SEND_UBX(UBX_CLASS_CFG, UBX_ID_CFG_MSG, UBX_PAYLOAD_MSG_OFF_GSV);
    SEND_UBX(UBX_CLASS_CFG, UBX_ID_CFG_MSG, UBX_PAYLOAD_MSG_OFF_GSA);
    SEND_UBX(UBX_CLASS_CFG, UBX_ID_CFG_MSG, UBX_PAYLOAD_MSG_OFF_GLL);
    SEND_UBX(UBX_CLASS_CFG, UBX_ID_CFG_MSG, UBX_PAYLOAD_MSG_OFF_VTG);
    // Step 2: automotive dynamic model
    SEND_UBX(UBX_CLASS_CFG, UBX_ID_CFG_NAV5, UBX_PAYLOAD_NAV5_AUTO);
    // Step 3: 5 Hz
    SEND_UBX(UBX_CLASS_CFG, UBX_ID_CFG_RATE, UBX_PAYLOAD_RATE_5HZ);
    // Step 4: UART1 38400 baud (GPS switches baud immediately)
    SEND_UBX(UBX_CLASS_CFG, UBX_ID_CFG_PRT, UBX_PAYLOAD_PRT_38400);
    return true;
}

// Step 5: save to EEPROM (sent at 38400 after reopening).
static void gps_save() {
    SEND_UBX(UBX_CLASS_CFG, UBX_ID_CFG_CFG, UBX_PAYLOAD_CFG_SAVE);
}

static void commit_fix(uint32_t now_ms) {
    uint32_t t = tiny.time.value();              // hhmmsscc; also clears the updated flag
    if (!tiny.location.isValid() || !tiny.time.isValid()) return;
    uint32_t h = t / 1000000UL;
    uint32_t m = (t / 10000UL) % 100UL;
    uint32_t s = (t / 100UL) % 100UL;
    uint32_t cs = t % 100UL;
    g.fix_utc_ms = (h * 3600UL + m * 60UL + s) * 1000UL + cs * 10UL;
    g.fix = true;
    g.last_fix_ms = now_ms;
    g.millis_at_fix = now_ms;
    g.lat_e7 = (int32_t)lround(tiny.location.lat() * 1e7);
    g.lon_e7 = (int32_t)lround(tiny.location.lng() * 1e7);
}

void gps_init() {
    g.fix_utc_ms = TIMESTAMP_NONE;               // §S8.2: no fix yet
    GPSSerial.begin(GPS_BAUD);
    uint32_t t0 = millis();
    uint32_t cs0 = tiny.passedChecksum();
    bool nmea = false;
    while ((uint32_t)(millis() - t0) < GPS_BOOT_NMEA_TIMEOUT_MS) {
        while (GPSSerial.available() > 0) tiny.encode((char)GPSSerial.read());
        if (tiny.passedChecksum() != cs0) { nmea = true; break; }
    }
    if (nmea) return;                            // already talking at 38400

    GPSSerial.begin(GPS_BAUD_DEFAULT);           // factory default 9600
    gps_configure();
    delay(GPS_BAUD_SWITCH_WAIT_MS);
    GPSSerial.begin(GPS_BAUD);
    gps_save();
}

void gps_poll(uint32_t now_ms) {
    while (GPSSerial.available() > 0) tiny.encode((char)GPSSerial.read());

    if (tiny.time.isUpdated()) {                 // new RMC or GGA sentence
        commit_fix(now_ms);
        if (g.fix) {
            if (tiny.speed.isValid()) g.speed_ms = tiny.speed.mps();
            if (g.speed_ms >= MIN_SPEED_FCW && tiny.course.isValid()) {
                g.heading_deg = tiny.course.deg();
            }
            if (tiny.satellites.isValid()) {
                uint32_t s = tiny.satellites.value();
                g.sats = (uint8_t)(s > 255 ? 255 : s);
            }
            if (tiny.hdop.isValid()) g.hdop = tiny.hdop.hdop();
        }
        g.gps_valid = g.fix && (g.sats >= GPS_MIN_SATS);
        uint32_t sq = g.sats > 15 ? 15 : g.sats;
        long hq = lround(g.hdop);
        uint32_t hd = hq < 0 ? 0 : (hq > 15 ? 15 : (uint32_t)hq);
        g.gps_quality = (uint8_t)((sq << 4) | hd);
    }

    if (g.fix && (uint32_t)(now_ms - g.last_fix_ms) > GPS_FIX_TIMEOUT_MS) {
        g.fix = false;
        g.gps_valid = false;
    }
}

const gps_state_t *gps_get() {
    return &g;
}

uint32_t gps_utc_now_ms(uint32_t now_ms) {
    if (g.fix_utc_ms == TIMESTAMP_NONE) return TIMESTAMP_NONE;
    uint64_t sum = (uint64_t)g.fix_utc_ms + (uint64_t)(uint32_t)(now_ms - g.millis_at_fix);
    return (uint32_t)(sum % GPS_DAY_MS);
}

void gps_sim_set(const gps_state_t *s) {
    if (s != NULL) g = *s;
}
