// Phase 4 driver test. Compiles the drivers from firmware/v2v_obu (see tools/sync_drivertest.py
// or the --library build in the Phase 4 notes). Output over USB Serial, 115200 (CDC ignores baud).
//   Once per second: GPS, IMU, battery, button, radio counters.
//   Every received radio packet: hex, RSSI, SNR, pkt_validate result.
//   Input: digit 1-7 plays alert pattern 1-7 for 3 s; 'p' sends one test BSM.
#include <string.h>
#include <math.h>
#include "config.h"
#include "v2v_packet.h"
#include "radio.h"
#include "gps.h"
#include "imu.h"
#include "power.h"
#include "alerts.h"
#include "button.h"

#define TEST_ALERT_MS   3000
#define STATUS_PERIOD_MS 1000
#define IMU_PERIOD_MS    (1000 / IMU_RATE_HZ)

static radio_rx_t rx;
static seq_table_t seq_tab;
static uint8_t bsm_seq = 0;
static uint32_t next_imu_ms = 0;
static uint32_t next_power_ms = 0;
static uint32_t next_status_ms = 0;

static const char *const validate_names[] = {
    "OK", "ERR_LEN", "ERR_START", "ERR_CRC", "ERR_OWN", "ERR_DUP", "ERR_STALE", "ERR_TARGET"
};

static void print_hex(const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (b[i] < 0x10) Serial.print('0');
        Serial.print(b[i], HEX);
        if (i + 1 < n) Serial.print(' ');
    }
}

static void build_bsm(v2v_packet_t *p, uint32_t now) {
    const gps_state_t *g = gps_get();
    const imu_state_t *im = imu_get();
    const power_state_t *pw = power_get();
    memset(p, 0, sizeof(*p));
    p->vehicle_id = MY_VEHICLE_ID;
    p->timestamp = gps_utc_now_ms(now);
    p->lat_e7 = g->lat_e7;
    p->lon_e7 = g->lon_e7;
    p->speed_cms = (uint16_t)lround(g->speed_ms * 100.0);
    p->heading_cdeg = (uint16_t)lround(g->heading_deg * 100.0);
    p->accel_long_cms2 = (int16_t)lround(im->a_long * 100.0);
    p->roll_deg = (int8_t)lround(im->roll);
    p->pitch_deg = (int8_t)lround(im->pitch);
    p->yaw_rate_cdps = (int16_t)lround(im->gz * 100.0);
    p->flags = 0;
    if (g->gps_valid) p->flags |= FLAG_GPS_VALID;
    if (pw->low) p->flags |= FLAG_LOW_BATTERY;
    p->battery_pct = pw->pct;
    p->gps_quality = g->gps_quality;
    pkt_build(p, PKT_BSM, bsm_seq++);
}

static void handle_rx(uint32_t now) {
    rx_ctx_t ctx;
    ctx.own_id = MY_VEHICLE_ID;
    ctx.own_gps_valid = gps_get()->gps_valid;
    ctx.local_utc_ms = gps_utc_now_ms(now);
    ctx.sim_mode = SIM_MODE;
    v2v_packet_t out;
    pkt_result_t r = pkt_validate(rx.data, rx.len, &ctx, &seq_tab, now, &out);
    if (r == PKT_ERR_CRC) radio_count_crc_error();
    Serial.print("RX len=");
    Serial.print(rx.len);
    Serial.print(" rssi=");
    Serial.print(rx.rssi_dbm);
    Serial.print(" snr=");
    Serial.print(rx.snr_cdb / 10.0, 1);
    Serial.print(" valid=");
    Serial.print(validate_names[r]);
    Serial.print(" hex: ");
    print_hex(rx.data, rx.len);
    Serial.println();
}

static void print_status(uint32_t now) {
    const gps_state_t *g = gps_get();
    const imu_state_t *im = imu_get();
    const power_state_t *pw = power_get();
    Serial.print("t="); Serial.print(now);
    Serial.print(" GPS fix="); Serial.print(g->fix);
    Serial.print(" sats="); Serial.print(g->sats);
    Serial.print(" hdop="); Serial.print(g->hdop, 1);
    Serial.print(" lat="); Serial.print(g->lat_e7 / 1e7, 6);
    Serial.print(" lon="); Serial.print(g->lon_e7 / 1e7, 6);
    Serial.print(" spd="); Serial.print(g->speed_ms, 2);
    Serial.print(" hdg="); Serial.print(g->heading_deg, 1);
    Serial.print(" utc="); Serial.print(gps_utc_now_ms(now));
    Serial.print(" valid="); Serial.println(g->gps_valid);
    Serial.print("IMU ok="); Serial.print(im->ok);
    Serial.print(" ax="); Serial.print(im->ax, 2);
    Serial.print(" ay="); Serial.print(im->ay, 2);
    Serial.print(" az="); Serial.print(im->az, 2);
    Serial.print(" gx="); Serial.print(im->gx, 1);
    Serial.print(" gy="); Serial.print(im->gy, 1);
    Serial.print(" gz="); Serial.print(im->gz, 1);
    Serial.print(" roll="); Serial.print(im->roll, 1);
    Serial.print(" pitch="); Serial.print(im->pitch, 1);
    Serial.print(" a_long="); Serial.print(im->a_long, 2);
    Serial.print(" a_tot_g="); Serial.println(im->a_total_g, 2);
    Serial.print("BAT vbat="); Serial.print(pw->vbat, 2);
    Serial.print(" pct="); Serial.print(pw->pct);
    Serial.print(" raw="); Serial.print(pw->raw);
    Serial.print(" low="); Serial.print(pw->low);
    Serial.print(" cutoff="); Serial.println(pw->cutoff);
    Serial.print("BTN pressed="); Serial.print(button_pressed());
    Serial.print(" held_ms="); Serial.println(button_held_ms(now));
    Serial.print("RADIO ok="); Serial.print(radio_ok());
    Serial.print(" tx="); Serial.print(radio_tx_total());
    Serial.print(" crc="); Serial.println(radio_crc_errors());
}

static void handle_serial(uint32_t now) {
    while (Serial.available() > 0) {
        int c = Serial.read();
        if (c >= '1' && c <= '7') {
            uint8_t pattern = (uint8_t)(c - '0');
            alert_set(pattern, TEST_ALERT_MS, now);
            Serial.print("alert pattern "); Serial.println(pattern);
        } else if (c == 'p') {
            v2v_packet_t p;
            build_bsm(&p, now);
            bool ok = radio_send(&p);
            Serial.print("test BSM tx "); Serial.println(ok ? "ok" : "FAILED");
        }
    }
}

void setup() {
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && (uint32_t)(millis() - t0) < 3000) { }   // wait for USB host, max 3 s
    Serial.println("drivertest: init");
    seq_table_init(&seq_tab);
    power_init();
    alerts_init();
    alert_lamp_test();
    button_init();
    gps_init();
    Serial.println(imu_init() ? "IMU init ok" : "IMU init FAILED");
    Serial.println("IMU calibrating, keep still 2 s");
    imu_calibrate();
    Serial.println(radio_init() ? "radio init ok" : "radio init FAILED");
    Serial.println("ready: digits 1-7 = alert pattern, p = test BSM");
}

void loop() {
    uint32_t now = millis();

    gps_poll(now);
    if ((uint32_t)(now - next_imu_ms) >= IMU_PERIOD_MS) {
        next_imu_ms = now;
        imu_update();
    }
    button_update(now);
    if (button_fell()) Serial.println("button pressed");

    if (radio_service(now, &rx)) handle_rx(now);
    alert_update(now);

    if ((uint32_t)(now - next_power_ms) >= 1000) {
        next_power_ms = now;
        power_read();
        radio_set_enabled(!power_get()->cutoff);
    }
    handle_serial(now);

    if ((uint32_t)(now - next_status_ms) >= STATUS_PERIOD_MS) {
        next_status_ms = now;
        print_status(now);
    }
}
