// v2v_obu.ino - Phase 5: full OBU firmware (spec S10.5 scheduler, S4.5 states, S7, S8, S9, S11.4).
//
// Owner of g_app (app_state.h). Gateway code (gateway.cpp) is compiled only when IS_GATEWAY=1.
// Loop rules: no blocking calls. The only blocking work is the radio TX (radio_send, ~12 ms per
// packet, by design of the Phase 4 driver) and the boot sequence in setup().
// Debug text is written only as far as the UART / USB transmit buffers have room (never waits).

#include <Arduino.h>
#include <IWatchdog.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "v2v_packet.h"
#include "geo.h"
#include "neighbors.h"
#include "safety.h"
#include "radio.h"
#include "gps.h"
#include "imu.h"
#include "power.h"
#include "alerts.h"
#include "button.h"
#include "sim.h"
#include "app_state.h"

app_state_t g_app;

static seq_table_t seq_tab;
static uint8_t own_seq = 0;

static brake_det_t brake_det;
static crash_det_t crash_det;
static rollover_det_t roll_det;
static pothole_det_t pothole_det;
static fcw_hyst_t fcw_hyst;
static bool fcw_critical_owned = false;   // FCW set ALERT_FCW_CRITICAL (so it may clear it)
static bool fcw_caution_owned = false;    // FCW set ALERT_FCW_CAUTION

static bool sos_active = false;
static uint8_t sos_cause = 0;             // FLAG_CRASH or FLAG_ROLLOVER while sos_active
static uint32_t sos_since_ms = 0;
static uint32_t sos_next_tx_ms = 0;       // next periodic CRASH packet (S7.2: every 1 s)

static uint32_t next_bsm_ms = 0;
static uint32_t next_imu_ms = 0;
static uint32_t next_fcw_ms = 0;
static uint32_t next_gw_ms = 0;
static uint32_t next_sec_ms = 0;

static radio_rx_t rx_buf;

// Debug line: formatted once per second, drained to each port without blocking.
static char log_buf[DEBUG_LINE_MAX];
static size_t log_len = 0;
static size_t log_pos_uart = 0;
#if !IS_GATEWAY
static size_t log_pos_usb = 0;
#endif

// ---------------------------------------------------------------- helpers

// True once per period. Re-arms from now, so a late call never bursts.
static bool due(uint32_t now, uint32_t *next, uint32_t period) {
    if ((int32_t)(now - *next) < 0) return false;
    *next = now + period;
    return true;
}

// Rounds to the nearest integer and clamps to [lo, hi]. NaN gives 0.
static long clamp_round(double v, long lo, long hi) {
    if (!isfinite(v)) return 0;
    if (v < (double)lo) return lo;
    if (v > (double)hi) return hi;
    return lround(v);
}

#if LOG_MODE != 1   // 1 Hz text log: not used in the data-logging build
static const char *state_name(uint8_t st) {
    switch (st) {
    case OBU_BOOT:    return "BOOT";
    case OBU_NORMAL:  return "NORMAL";
    case OBU_WARNING: return "WARN";
    case OBU_SOS:     return "SOS";
    default:          return "?";
    }
}
#endif

// Appends formatted text at offset `at`, never past `cap - 1`. Returns the new offset.
static size_t put(char *buf, size_t cap, size_t at, const char *fmt, ...) {
    if (at + 1 >= cap) return at;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + at, cap - at, fmt, ap);
    va_end(ap);
    if (n < 0) return at;
    size_t next = at + (size_t)n;
    return (next >= cap) ? cap - 1 : next;
}

// ---------------------------------------------------------------- app_state.h interface

uint8_t app_next_seq(void) {
    return own_seq++;
}

// Flags per S8.3, from the current detector and alert state.
static uint8_t own_flags(uint32_t now) {
    uint8_t f = 0;
    if (brake_flag_active(&brake_det, now)) f |= FLAG_HARD_BRAKE;
    if (sos_active) f |= sos_cause;
    if (pothole_flag_active(&pothole_det, now)) f |= FLAG_HAZARD;
    if (g_app.warn_level == FCW_CAUTION) f |= FLAG_WARN_CAUTION;
    if (g_app.warn_level == FCW_CRITICAL) f |= FLAG_WARN_CRITICAL;
    if (g_app.own.gps_valid) f |= FLAG_GPS_VALID;
    if (power_get()->low) f |= FLAG_LOW_BATTERY;
    return f;
}

// Zeroes p and fills every own-vehicle field of S8.2. Callers overwrite what their packet type
// needs (type-specific fields, target, seq and crc are set by pkt_build).
void app_fill_header(v2v_packet_t *p) {
    const gps_state_t *g = gps_get();
    const imu_state_t *im = imu_get();
    const power_state_t *pw = power_get();

    memset(p, 0, sizeof *p);
    p->vehicle_id = MY_VEHICLE_ID;
    p->timestamp = g->fix_utc_ms;                                // fix time, TIMESTAMP_NONE without GPS
    p->lat_e7 = g->lat_e7;
    p->lon_e7 = g->lon_e7;
    p->speed_cms = (uint16_t)clamp_round(g->speed_ms * 100.0, 0, 65535);
    p->heading_cdeg = (uint16_t)clamp_round(g->heading_deg * 100.0, 0, 35999);
    p->accel_long_cms2 = (int16_t)clamp_round(im->a_long * 100.0, -32768, 32767);
    p->roll_deg = (int8_t)clamp_round(im->roll, -128, 127);
    p->pitch_deg = (int8_t)clamp_round(im->pitch, -128, 127);
    p->yaw_rate_cdps = (int16_t)clamp_round(im->gz * 100.0, -32768, 32767);
    p->flags = own_flags(millis());
    p->battery_pct = pw->pct;
    p->gps_quality = g->gps_quality;
    p->target_id = 0;
}

static void leave_sos(void) {
    sos_active = false;
    sos_cause = 0;
    alert_clear(ALERT_SOS_LOCAL);
    crash_init(&crash_det);
    rollover_init(&roll_det);
}

// §S11.4 "Vehicle on COMMAND" for the own vehicle. The ACK is sent by the receive path.
void app_execute_command(uint8_t code, uint16_t param, uint32_t now_ms) {
    switch (code) {
    case CMD_CODE_HAZARD_ALERT:
        alert_set(ALERT_HAZARD, ALERT_CMD_HAZARD_MS, now_ms);
        break;
    case CMD_CODE_SPEED_ADVISORY: {
        double own_kmh = g_app.own.speed_ms * KMH_PER_MS;
        if (own_kmh > (double)param) {
            alert_set(ALERT_FCW_CAUTION, ALERT_CMD_FCW_MS, now_ms);
        } else {
            alert_beep(ALERT_CAUTION_BEEP_MS, now_ms);
        }
        break;
    }
    case CMD_CODE_CLEAR_SOS:
        if (sos_active) leave_sos();
        break;
    case CMD_CODE_BUZZER_TEST:
        alert_beep(ALERT_CMD_BUZZER_MS, now_ms);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- own state and SOS

static void update_own_state(void) {
    const gps_state_t *g = gps_get();    // real GPS, or the simulated state in SIM_MODE
    g_app.own.lat_e7 = g->lat_e7;
    g_app.own.lon_e7 = g->lon_e7;
    g_app.own.speed_ms = g->speed_ms;
    g_app.own.heading_deg = g->heading_deg;
    g_app.own.gps_valid = g->gps_valid;
}

static void enter_sos(uint8_t cause, uint32_t now) {
    sos_active = true;
    sos_cause = cause;
    sos_since_ms = now;
    sos_next_tx_ms = now + SOS_PERIOD_MS;
    alert_set(ALERT_SOS_LOCAL, 0, now);
    v2v_packet_t p;
    app_fill_header(&p);                        // flags now include CRASH or ROLLOVER
    pkt_build(&p, PKT_CRASH, app_next_seq());
    radio_queue_event(&p, now);                 // 3 copies, same seq
}

// Button held SOS_CANCEL_HOLD_MS. The press must start after the SOS began, so a button that
// was already held when the crash happened does not cancel it at once.
static bool sos_cancel_held(uint32_t now) {
    uint32_t held = button_held_ms(now);
    return held >= SOS_CANCEL_HOLD_MS && (uint32_t)(now - sos_since_ms) >= held;
}

// ---------------------------------------------------------------- transmit helpers

// Builds an own packet of the given type (single copy, for direct send).
static void build_own(v2v_packet_t *p, uint8_t type) {
    app_fill_header(p);
    pkt_build(p, type, app_next_seq());
}

static void send_event(uint8_t type, uint32_t now) {    // EEBL / HAZARD: 3 copies
    v2v_packet_t p;
    build_own(&p, type);
    radio_queue_event(&p, now);
}

static void send_bsm(void) {
    v2v_packet_t p;
    build_own(&p, PKT_BSM);
    g_app.own_pkt = p;
    radio_send(&p);
}

static void send_ack(const v2v_packet_t *cmd) {
    v2v_packet_t a;
    app_fill_header(&a);
    a.speed_cms = cmd->speed_cms;               // ACK: echoes the command code
    a.target_id = cmd->vehicle_id;
    pkt_build(&a, PKT_ACK, app_next_seq());
    radio_send(&a);
}

static void send_pong(const v2v_packet_t *ping) {
    v2v_packet_t q;
    app_fill_header(&q);
    q.timestamp = ping->timestamp;              // echo: gateway computes RTT = millis() - timestamp
    q.target_id = ping->vehicle_id;
    pkt_build(&q, PKT_PONG, app_next_seq());
    radio_send(&q);
}

// ---------------------------------------------------------------- receive path (S8.6, S9.4-9.7, S11.4)

static void handle_rx(const radio_rx_t *rx, uint32_t now) {
    rx_ctx_t ctx;
    ctx.own_id = MY_VEHICLE_ID;
    ctx.own_gps_valid = g_app.own.gps_valid;
    ctx.local_utc_ms = gps_utc_now_ms(now);
    ctx.sim_mode = (SIM_MODE != 0);

    v2v_packet_t pkt;
    pkt_result_t r = pkt_validate(rx->data, rx->len, &ctx, &seq_tab, now, &pkt);
    if (r == PKT_ERR_CRC) {
        radio_count_crc_error();                // counted in the radio's counter (HW + SW)
        return;
    }
    if (r != PKT_OK) return;

    g_app.rx_valid_total++;
    nb_update(&pkt, rx->rssi_dbm, rx->snr_cdb, now);
#if IS_GATEWAY
    gw_on_packet(&pkt, now);
#endif

    switch (pkt.type) {
    case PKT_EEBL:
        if (should_alert_eebl(&g_app.own, &pkt)) alert_set(ALERT_EEBL, ALERT_EEBL_RX_MS, now);
        break;
    case PKT_CRASH:
        alert_set(ALERT_SOS_REMOTE, ALERT_SOS_REMOTE_RX_MS, now);
        break;
    case PKT_HAZARD:
        if (should_alert_hazard(&g_app.own, &pkt)) alert_set(ALERT_HAZARD, ALERT_HAZARD_RX_MS, now);
        break;
    case PKT_COMMAND: {                         // target already checked by pkt_validate
        uint8_t code = (uint8_t)pkt.speed_cms;
        if (code >= CMD_CODE_HAZARD_ALERT && code <= CMD_CODE_BUZZER_TEST) {
            app_execute_command(code, pkt.heading_cdeg, now);
            send_ack(&pkt);
        }
        break;
    }
    case PKT_PING:
        send_pong(&pkt);
        break;
    default:                                    // BSM, ACK, PONG: nothing to do on a vehicle
        break;
    }
}

// ---------------------------------------------------------------- 100 Hz detectors (S9.4-9.7)

static void run_detectors(uint32_t now) {
    if (!imu_update()) {
        g_app.imu_ok = false;
        return;
    }
    g_app.imu_ok = true;
    const imu_state_t *im = imu_get();
    const own_state_t *own = &g_app.own;

    if (brake_update(&brake_det, im->a_long, own->speed_ms, own->gps_valid, now)) {
        send_event(PKT_EEBL, now);
    }
    bool crash = crash_update(&crash_det, im->a_total_g, own->speed_ms, own->gps_valid, now);
    bool roll = rollover_update(&roll_det, im->roll, im->pitch, now);
    if (!sos_active) {
        if (crash) {
            enter_sos(FLAG_CRASH, now);
        } else if (roll) {
            enter_sos(FLAG_ROLLOVER, now);
        }
    }
    if (pothole_update(&pothole_det, im->az_hp, own->speed_ms, own->gps_valid, now)) {
        send_event(PKT_HAZARD, now);
    }
}

// ---------------------------------------------------------------- data logging CSV (LOG_MODE == 1, Phase 8)
#if LOG_MODE == 1
// Header line, sent once per port (and again after a USB reconnect). Fixed-point columns, see config.h.
static const char csv_header[] =
    "ms,a_long_x100,a_total_x100,az_hp_x100,roll_x100,pitch_x100,speed_x100,level,ttc_x100\n";
#define CSV_HDR_LEN (sizeof(csv_header) - 1)

static char csv_line[LOG_CSV_LINE_MAX];   // the one pending row
static size_t csv_len = 0;                // length of the pending row, 0 = none
static size_t csv_pos_uart = 0;           // bytes of csv_line already written to Serial1
static size_t csv_hdr_uart = 0;           // bytes of csv_header already written to Serial1
#if !IS_GATEWAY
static size_t csv_pos_usb = 0;
static size_t csv_hdr_usb = 0;
#endif

// Called at 20 Hz from run_fcw(). A row that does not fit the free port buffers is dropped, never waited for.
static void csv_emit(uint32_t now) {
    if (csv_len != 0) return;                 // previous row still on its way: drop this one
    const imu_state_t *im = imu_get();
    const long lim = LOG_CSV_LIMIT;
    long ttc_x = isfinite(g_app.min_ttc_s)
                     ? clamp_round(g_app.min_ttc_s * LOG_CSV_SCALE, 0, lim)
                     : LOG_CSV_TTC_NONE;
    size_t at = put(csv_line, LOG_CSV_LINE_MAX, 0, "%lu,%ld,%ld,%ld,%ld,%ld,%ld,%u,%ld\n",
                    (unsigned long)now,
                    clamp_round(im->a_long * LOG_CSV_SCALE, -lim, lim),
                    clamp_round(im->a_total_g * LOG_CSV_SCALE, -lim, lim),
                    clamp_round(im->az_hp * LOG_CSV_SCALE, -lim, lim),
                    clamp_round(im->roll * LOG_CSV_SCALE, -lim, lim),
                    clamp_round(im->pitch * LOG_CSV_SCALE, -lim, lim),
                    clamp_round(g_app.own.speed_ms * LOG_CSV_SCALE, -lim, lim),
                    (unsigned)g_app.warn_level,
                    ttc_x);
    csv_len = at;
    csv_pos_uart = (csv_hdr_uart >= CSV_HDR_LEN) ? 0 : csv_len;   // header not out yet: skip this row
#if !IS_GATEWAY
    csv_pos_usb = (Serial && csv_hdr_usb >= CSV_HDR_LEN) ? 0 : csv_len;
#endif
}

// Writes the header and the pending row to each port, as far as its transmit buffer has room.
static void csv_pump(void) {
    if (csv_hdr_uart < CSV_HDR_LEN) {
        size_t room = (size_t)Serial1.availableForWrite();
        size_t want = CSV_HDR_LEN - csv_hdr_uart;
        if (want > room) want = room;
        if (want > 0) csv_hdr_uart += Serial1.write((const uint8_t *)csv_header + csv_hdr_uart, want);
    }
    if (csv_pos_uart < csv_len) {
        size_t room = (size_t)Serial1.availableForWrite();
        size_t want = csv_len - csv_pos_uart;
        if (want > room) want = room;
        if (want > 0) csv_pos_uart += Serial1.write((const uint8_t *)csv_line + csv_pos_uart, want);
    }
#if !IS_GATEWAY
    if (!Serial) {                            // no host on USB: drop the row, send the header on connect
        csv_pos_usb = csv_len;
        csv_hdr_usb = 0;
    } else {
        if (csv_hdr_usb < CSV_HDR_LEN) {
            size_t room = (size_t)Serial.availableForWrite();
            size_t want = CSV_HDR_LEN - csv_hdr_usb;
            if (want > room) want = room;
            if (want > 0) csv_hdr_usb += Serial.write((const uint8_t *)csv_header + csv_hdr_usb, want);
        }
        if (csv_pos_usb < csv_len) {
            size_t room = (size_t)Serial.availableForWrite();
            size_t want = csv_len - csv_pos_usb;
            if (want > room) want = room;
            if (want > 0) csv_pos_usb += Serial.write((const uint8_t *)csv_line + csv_pos_usb, want);
        }
    }
    if (csv_pos_uart >= csv_len && csv_pos_usb >= csv_len) csv_len = 0;
#else
    if (csv_pos_uart >= csv_len) csv_len = 0;
#endif
}
#endif  // LOG_MODE == 1

// ---------------------------------------------------------------- 20 Hz FCW (S9.3)

static void run_fcw(uint32_t now) {
    nb_expire(now);
    const own_state_t *own = &g_app.own;
    uint8_t raw_max = FCW_SAFE;
    double min_ttc = INFINITY;
    uint8_t n = nb_count();

    for (uint8_t i = 0; i < MAX_NEIGHBORS; i++) {
        app_fcw_entry_t *e = &g_app.fcw[i];
        if (i >= n) {
            e->id = 0;
            e->level = FCW_SAFE;
            e->ttc_s = INFINITY;
            e->distance_m = INFINITY;
            continue;
        }
        const neighbor_t *nb = nb_get(i);
        fcw_result_t r = fcw_evaluate(own, &nb->last);
        e->id = nb->id;
        e->level = r.level;
        e->ttc_s = r.ttc_s;
        e->distance_m = r.distance_m;
        if (r.level > raw_max) raw_max = r.level;
        if (r.ttc_s < min_ttc) min_ttc = r.ttc_s;
    }

    g_app.warn_level = fcw_hyst_update(&fcw_hyst, raw_max, now);
    g_app.min_ttc_s = min_ttc;

    // Only clear an alert that FCW itself set, so a command-triggered caution keeps its 3 s.
    if (g_app.warn_level == FCW_CRITICAL) {
        alert_set(ALERT_FCW_CRITICAL, 0, now);
        fcw_critical_owned = true;
    } else if (fcw_critical_owned) {
        alert_clear(ALERT_FCW_CRITICAL);
        fcw_critical_owned = false;
    }
    if (g_app.warn_level == FCW_CAUTION) {
        alert_set(ALERT_FCW_CAUTION, 0, now);
        fcw_caution_owned = true;
    } else if (fcw_caution_owned) {
        alert_clear(ALERT_FCW_CAUTION);
        fcw_caution_owned = false;
    }

#if LOG_MODE == 1
    csv_emit(now);                            // Phase 8: 20 Hz data-logging row
#endif
}

// ---------------------------------------------------------------- debug log (Serial1, and USB when not a gateway)

// Seconds with one decimal place ("3.5"), or "inf" for INFINITY. No float printf (nano libc).
#if LOG_MODE != 1
static void fmt_tenths(char *out, size_t cap, double v) {
    if (!isfinite(v)) {
        snprintf(out, cap, "inf");
        return;
    }
    long t = lround(v * 10.0);
    long mag = (t < 0) ? -t : t;
    snprintf(out, cap, "%s%ld.%ld", (t < 0) ? "-" : "", mag / 10, mag % 10);
}

static void build_log(uint32_t now) {
    if (log_len != 0 && log_pos_uart < log_len) return;   // previous line still on Serial1: skip this second

    const gps_state_t *g = gps_get();
    const power_state_t *pw = power_get();
    char tb[24];
    size_t at = 0;
    at = put(log_buf, DEBUG_LINE_MAX, at,
             "S=%s t=%lu pos=%ld,%ld spd=%lucms hdg=%lu sats=%u gps=%u |",
             state_name(g_app.obu_state), (unsigned long)now,
             (long)g->lat_e7, (long)g->lon_e7,
             (unsigned long)clamp_round(g->speed_ms * 100.0, 0, 65535),
             (unsigned long)clamp_round(g->heading_deg, 0, 359),
             (unsigned)(g->gps_quality >> 4), (unsigned)g_app.own.gps_valid);

    for (uint8_t i = 0; i < MAX_NEIGHBORS; i++) {
        const app_fcw_entry_t *e = &g_app.fcw[i];
        if (e->id == 0) continue;
        const neighbor_t *nb = nb_find(e->id);
        fmt_tenths(tb, sizeof tb, e->ttc_s);
        at = put(log_buf, DEBUG_LINE_MAX, at,
                 " [id=%u d=%ldm ttc=%s lv=%u rssi=%d pdr=%u%%]",
                 (unsigned)e->id,
                 isfinite(e->distance_m) ? lround(e->distance_m) : -1L,
                 tb, (unsigned)e->level,
                 nb ? (int)nb->rssi_dbm : 0,
                 nb ? (unsigned)(nb->pdr_permille / 10) : 0u);
    }

    fmt_tenths(tb, sizeof tb, g_app.min_ttc_s);
    at = put(log_buf, DEBUG_LINE_MAX, at,
             " | lvl=%u minttc=%s | bat=%u%% low=%u | rx=%lu tx=%lu crc=%lu imu=%u\n",
             (unsigned)g_app.warn_level, tb,
             (unsigned)pw->pct, (unsigned)pw->low,
             (unsigned long)g_app.rx_valid_total, (unsigned long)radio_tx_total(),
             (unsigned long)g_app.crc_errors, (unsigned)g_app.imu_ok);

    log_len = at;
    log_pos_uart = 0;
#if !IS_GATEWAY
    log_pos_usb = Serial ? 0 : log_len;        // no host on USB: skip the mirror
#endif
}

#endif

// Writes as many pending bytes as each port has room for. Never waits.
static void log_pump(void) {
    if (log_len == 0) return;
    if (log_pos_uart < log_len) {
        size_t room = (size_t)Serial1.availableForWrite();
        size_t want = log_len - log_pos_uart;
        if (want > room) want = room;
        if (want > 0) log_pos_uart += Serial1.write((const uint8_t *)log_buf + log_pos_uart, want);
    }
#if !IS_GATEWAY
    if (log_pos_usb < log_len && Serial) {
        size_t room = (size_t)Serial.availableForWrite();
        size_t want = log_len - log_pos_usb;
        if (want > room) want = room;
        if (want > 0) log_pos_usb += Serial.write((const uint8_t *)log_buf + log_pos_usb, want);
    }
#endif
}

// ---------------------------------------------------------------- setup / loop

void setup() {
    Serial1.begin(DEBUG_BAUD);
#if !IS_GATEWAY
    Serial.begin(DEBUG_BAUD);                  // gateway builds: USB CDC belongs to Modbus
#endif
    g_app.obu_state = OBU_BOOT;

    power_init();
    alerts_init();
    alert_lamp_test();                         // 0.5 s lamp test + 100 ms beep (boot only)
    button_init();
    power_read();
    gps_init();                                // boot only, up to ~3 s
    g_app.imu_ok = imu_init();
    if (g_app.imu_ok) imu_calibrate();         // keep still 2 s (boot only)

    nb_init();
    seq_table_init(&seq_tab);
    brake_init(&brake_det);
    crash_init(&crash_det);
    rollover_init(&roll_det);
    pothole_init(&pothole_det);
    fcw_hyst_init(&fcw_hyst);
    for (uint8_t i = 0; i < MAX_NEIGHBORS; i++) {
        g_app.fcw[i].id = 0;
        g_app.fcw[i].level = FCW_SAFE;
        g_app.fcw[i].ttc_s = INFINITY;
        g_app.fcw[i].distance_m = INFINITY;
    }
    g_app.min_ttc_s = INFINITY;

    if (!radio_init()) {
        // Radio failure: red LED 5 Hz forever. The watchdog is deliberately not started on this
        // path, so the board stays in this pattern instead of resetting every 2 s.
        bool led = false;
        for (;;) {
            led = !led;
            digitalWrite(PIN_LED_RED, led ? ALERT_OUT_ON : ALERT_OUT_OFF);
            delay(LED_FAIL_BLINK_MS);
        }
    }
    g_app.radio_ok = true;

#if IS_GATEWAY
    gw_init();
#endif

    IWatchdog.begin(WATCHDOG_TIMEOUT_US);

    uint32_t now = millis();
    next_bsm_ms = now + (uint32_t)random(0, BSM_PERIOD_MS);   // first BSM within 200 ms
    next_imu_ms = now;
    next_fcw_ms = now;
    next_gw_ms = now;
    next_sec_ms = now;
    g_app.obu_state = OBU_NORMAL;
}

void loop() {
    uint32_t now = millis();

    // Position source: simulated (SIM_MODE) or NMEA from the GPS.
#if SIM_MODE
    sim_update(now);
#else
    gps_poll(now);
#endif
    update_own_state();

    if (due(now, &next_imu_ms, LOOP_IMU_PERIOD_MS)) run_detectors(now);
    if (due(now, &next_fcw_ms, LOOP_FCW_PERIOD_MS)) run_fcw(now);

    // Periodic BSM with random jitter (S7.2).
    if ((int32_t)(now - next_bsm_ms) >= 0) {
        next_bsm_ms = now + BSM_PERIOD_MS + (uint32_t)random(0, BSM_JITTER_MS + 1);
        send_bsm();
    }

    // SOS: CRASH packet every SOS_PERIOD_MS while in SOS (S7.2).
    if (sos_active && (int32_t)(now - sos_next_tx_ms) >= 0) {
        sos_next_tx_ms = now + SOS_PERIOD_MS;
        v2v_packet_t p;
        build_own(&p, PKT_CRASH);
        radio_send(&p);
    }

    // Event copies and received packets (one of each per call).
    if (radio_service(now, &rx_buf)) handle_rx(&rx_buf, now);
    g_app.crc_errors = radio_crc_errors();

    button_update(now);
    if (sos_active && sos_cancel_held(now)) leave_sos();

#if IS_GATEWAY
    gw_poll(now);
    if (due(now, &next_gw_ms, LOOP_GW_REG_PERIOD_MS)) gw_update_registers(now);
#endif

    alert_set_gps_fix(g_app.own.gps_valid);
    alert_update(now);

    // State machine (S4.5). WARNING covers any alert other than NORMAL that is active.
    if (sos_active) {
        g_app.obu_state = OBU_SOS;
    } else if (alert_active() != ALERT_NORMAL) {
        g_app.obu_state = OBU_WARNING;
    } else {
        g_app.obu_state = OBU_NORMAL;
    }

    if (due(now, &next_sec_ms, LOOP_SEC_PERIOD_MS)) {
        power_read();
        radio_set_enabled(!power_get()->cutoff);   // below VBAT_CUTOFF: TX off, RX on
#if LOG_MODE != 1                             // the 1 Hz text line is off in the data-logging build
        build_log(now);
#endif
    }
    log_pump();
#if LOG_MODE == 1
    csv_pump();
#endif

    IWatchdog.reload();
}
