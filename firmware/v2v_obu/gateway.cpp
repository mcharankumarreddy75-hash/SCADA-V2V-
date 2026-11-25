// Phase 6 gateway (spec S11). Compiled only when IS_GATEWAY = 1. Portable Modbus logic lives in
// modbus_rtu.cpp; this file does the Arduino I/O (USB Serial), register mapping and commands.
// Never print to USB Serial here: USB carries Modbus only (debug goes to Serial1).
#include "gateway.h"

#if IS_GATEWAY

#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "config.h"
#include "modbus_rtu.h"
#include "neighbors.h"
#include "radio.h"

static uint16_t s_regs[MODBUS_REG_COUNT];
static mb_slave_t s_mb;

// Plain-value view of one vehicle slot, encoded by put_slot().
typedef struct {
    uint16_t id;
    int32_t  lat_e7, lon_e7;
    uint16_t speed_cms, heading_cdeg;
    int16_t  accel_cms2, roll_deg, pitch_deg;
    uint16_t flags, battery_pct;
    int16_t  rssi_dbm, snr_cdb;
    uint16_t pdr_permille;
    uint16_t distance_dm, ttc_ds;
    uint16_t warn;
    uint16_t gps_quality;
    uint16_t age_ms;
} gw_slot_t;

static bool reg_is_reserved(uint16_t addr) {
    return (addr >= GW_RESERVED_LO_FIRST && addr <= GW_RESERVED_LO_LAST) ||
           (addr >= GW_RESERVED_HI_FIRST && addr <= GW_RESERVED_HI_LAST);
}

// Called by the Modbus slave after each register write. Clears reserved registers and runs commands.
static void on_write(void *ctx, uint16_t addr, uint16_t value) {
    (void)ctx;
    if (reg_is_reserved(addr)) {
        s_regs[addr] = 0;
        return;
    }
    if (addr == GW_REG_CMD_TRIGGER && value == GW_TRIGGER_VALUE) {
        // Validate the full 16-bit values BEFORE narrowing: a target of 256 must not wrap to 0
        // (broadcast) and a code of 257 must not wrap to 1 (§S11.4).
        uint16_t code = s_regs[GW_REG_CMD_CODE];
        uint16_t target = s_regs[GW_REG_CMD_TARGET];
        if (code >= GW_CMD_HAZARD_ALERT && code <= GW_CMD_PING && target <= GW_CMD_TARGET_MAX) {
            gw_run_command((uint8_t)code, s_regs[GW_REG_CMD_PARAM], (uint8_t)target, millis());
        }
        s_regs[GW_REG_CMD_TRIGGER] = 0;
    }
}

void gw_init(void) {
    memset(s_regs, 0, sizeof s_regs);
    mb_init(&s_mb, MODBUS_SLAVE_ID, s_regs, on_write, NULL);
    Serial.begin(MODBUS_BAUD);
}

void gw_poll(uint32_t now_ms) {
    (void)now_ms;
    int avail = Serial.available();
    while (avail-- > 0) {
        int c = Serial.read();
        if (c < 0) {
            break;
        }
        size_t n = mb_feed(&s_mb, (uint8_t)c, (uint32_t)micros());
        if (n > 0) {
            Serial.write(s_mb.tx, n);
        }
    }
}

void gw_run_command(uint8_t code, uint16_t param, uint8_t target, uint32_t now_ms) {
    if (code < GW_CMD_HAZARD_ALERT || code > GW_CMD_PING) {
        return;
    }
    v2v_packet_t p;
    app_fill_header(&p);
    p.target_id = target;
    if (code == GW_CMD_PING) {
        p.timestamp = millis();
        pkt_build(&p, PKT_PING, app_next_seq());
        radio_send(&p);            // once, not the 3-copy event queue
        return;
    }
    p.speed_cms = code;            // COMMAND: code
    p.heading_cdeg = param;        // COMMAND: parameter
    pkt_build(&p, PKT_COMMAND, app_next_seq());
    radio_queue_event(&p, now_ms); // EVENT_COPIES copies, same seq
    if (code <= GW_CMD_MAX_LOCAL && (target == MODBUS_BROADCAST_ID || target == MY_VEHICLE_ID)) {
        app_execute_command(code, param, now_ms);
    }
}

void gw_on_packet(const v2v_packet_t *pkt, uint32_t now_ms) {
    if (pkt->type == PKT_ACK) {
        if (pkt->target_id == MY_VEHICLE_ID) {
            s_regs[GW_REG_CMD_ACK_COUNT]++;
        }
    } else if (pkt->type == PKT_PONG) {
        uint32_t rtt = (uint32_t)(now_ms - pkt->timestamp);   // unsigned: safe across millis() wrap
        s_regs[GW_REG_LAST_RTT_MS] = (rtt > GW_NONE) ? GW_NONE : (uint16_t)rtt;
    }
}

static void put_slot_reg(uint8_t slot, uint8_t off, uint16_t v) {
    s_regs[GW_SLOT_BASE + GW_SLOT_SIZE * slot + off] = v;
}

static void put_slot_s32(uint8_t slot, uint8_t off_hi, int32_t v) {
    uint32_t u = (uint32_t)v;               // high word first
    put_slot_reg(slot, off_hi, (uint16_t)(u >> 16));
    put_slot_reg(slot, (uint8_t)(off_hi + 1), (uint16_t)(u & 0xFFFF));
}

static void put_slot(uint8_t slot, const gw_slot_t *v) {
    put_slot_reg(slot, GW_OFF_ID, v->id);
    put_slot_s32(slot, GW_OFF_LAT_HI, v->lat_e7);
    put_slot_s32(slot, GW_OFF_LON_HI, v->lon_e7);
    put_slot_reg(slot, GW_OFF_SPEED, v->speed_cms);
    put_slot_reg(slot, GW_OFF_HEADING, v->heading_cdeg);
    put_slot_reg(slot, GW_OFF_ACCEL, (uint16_t)v->accel_cms2);
    put_slot_reg(slot, GW_OFF_ROLL, (uint16_t)v->roll_deg);
    put_slot_reg(slot, GW_OFF_PITCH, (uint16_t)v->pitch_deg);
    put_slot_reg(slot, GW_OFF_FLAGS, v->flags);
    put_slot_reg(slot, GW_OFF_BATTERY, v->battery_pct);
    put_slot_reg(slot, GW_OFF_RSSI, (uint16_t)v->rssi_dbm);
    put_slot_reg(slot, GW_OFF_SNR, (uint16_t)v->snr_cdb);
    put_slot_reg(slot, GW_OFF_PDR, v->pdr_permille);
    put_slot_reg(slot, GW_OFF_DISTANCE, v->distance_dm);
    put_slot_reg(slot, GW_OFF_TTC, v->ttc_ds);
    put_slot_reg(slot, GW_OFF_WARN, v->warn);
    put_slot_reg(slot, GW_OFF_GPS_QUALITY, v->gps_quality);
    put_slot_reg(slot, GW_OFF_AGE_MS, v->age_ms);
}

static void clear_slot(uint8_t slot) {
    memset(&s_regs[GW_SLOT_BASE + GW_SLOT_SIZE * slot], 0, GW_SLOT_SIZE * sizeof(uint16_t));
}

// Seconds (or metres) -> 0.1 s (or 0.1 m) units; GW_NONE if not finite or out of range.
static uint16_t tenths_or_none(double value) {
    if (!isfinite(value) || value < 0.0) {
        return GW_NONE;
    }
    double x = round(value * 10.0);
    return (x > 65534.0) ? GW_NONE : (uint16_t)x;
}

static uint16_t age_u16(uint32_t now_ms, uint32_t seen_ms) {
    uint32_t age = (uint32_t)(now_ms - seen_ms);
    return (age > 65535u) ? 65535 : (uint16_t)age;
}

void gw_update_registers(uint32_t now_ms) {
    s_regs[GW_REG_FW_VERSION] = FW_VERSION;
    s_regs[GW_REG_STATUS] = (uint16_t)((g_app.radio_ok ? GW_STATUS_RADIO_OK : 0) |
                                       (g_app.own.gps_valid ? GW_STATUS_GPS_FIX : 0) |
                                       (g_app.imu_ok ? GW_STATUS_IMU_OK : 0) |
                                       (SIM_MODE ? GW_STATUS_SIM_MODE : 0));
    s_regs[GW_REG_NEIGHBOR_COUNT] = nb_count();
    s_regs[GW_REG_RX_TOTAL] = (uint16_t)g_app.rx_valid_total;
    s_regs[GW_REG_TX_TOTAL] = (uint16_t)radio_tx_total();
    s_regs[GW_REG_CRC_ERRORS] = (uint16_t)g_app.crc_errors;
    s_regs[GW_REG_GATEWAY_ID] = MY_VEHICLE_ID;
    s_regs[GW_REG_UPTIME_S] = (uint16_t)(now_ms / 1000u);
    for (uint16_t a = GW_RESERVED_LO_FIRST; a <= GW_RESERVED_LO_LAST; a++) {
        s_regs[a] = 0;
    }
    for (uint16_t a = GW_RESERVED_HI_FIRST; a <= GW_RESERVED_HI_LAST; a++) {
        s_regs[a] = 0;
    }

    // Slot 0: own vehicle (packet fields as built for transmission).
    const v2v_packet_t *own = &g_app.own_pkt;
    gw_slot_t self;
    memset(&self, 0, sizeof self);
    self.id = MY_VEHICLE_ID;
    self.lat_e7 = own->lat_e7;
    self.lon_e7 = own->lon_e7;
    self.speed_cms = own->speed_cms;
    self.heading_cdeg = own->heading_cdeg;
    self.accel_cms2 = own->accel_long_cms2;
    self.roll_deg = own->roll_deg;
    self.pitch_deg = own->pitch_deg;
    self.flags = own->flags;
    self.battery_pct = own->battery_pct;
    self.pdr_permille = GW_PDR_FULL;
    self.distance_dm = GW_NONE;
    self.ttc_ds = tenths_or_none(g_app.min_ttc_s);
    self.warn = g_app.warn_level;
    self.gps_quality = own->gps_quality;
    self.age_ms = 0;
    put_slot(0, &self);

    // Slots 1-3: neighbours in table order.
    for (uint8_t i = 0; i < GW_NB_SLOTS; i++) {
        const neighbor_t *nb = nb_get(i);
        if (nb == NULL) {
            clear_slot((uint8_t)(i + 1));
            continue;
        }
        // Distance and TTC from the FCW result for this neighbour, if present.
        uint16_t dist = GW_NONE;
        uint16_t ttc = GW_NONE;
        for (uint8_t j = 0; j < MAX_NEIGHBORS; j++) {
            if (nb->id != 0 && g_app.fcw[j].id == nb->id) {
                dist = tenths_or_none(g_app.fcw[j].distance_m);
                ttc = tenths_or_none(g_app.fcw[j].ttc_s);
                break;
            }
        }
        const v2v_packet_t *last = &nb->last;
        gw_slot_t v;
        memset(&v, 0, sizeof v);
        v.id = nb->id;
        v.lat_e7 = last->lat_e7;
        v.lon_e7 = last->lon_e7;
        v.speed_cms = last->speed_cms;
        v.heading_cdeg = last->heading_cdeg;
        v.accel_cms2 = last->accel_long_cms2;
        v.roll_deg = last->roll_deg;
        v.pitch_deg = last->pitch_deg;
        v.flags = last->flags;
        v.battery_pct = last->battery_pct;
        v.rssi_dbm = nb->rssi_dbm;
        v.snr_cdb = nb->snr_cdb;
        v.pdr_permille = nb->pdr_permille;
        v.distance_dm = dist;
        v.ttc_ds = ttc;
        v.warn = (last->flags & FLAG_WARN_CRITICAL) ? 2 : ((last->flags & FLAG_WARN_CAUTION) ? 1 : 0);
        v.gps_quality = last->gps_quality;
        v.age_ms = age_u16(now_ms, nb->last_seen_ms);
        put_slot((uint8_t)(i + 1), &v);
    }
}

#endif  // IS_GATEWAY
