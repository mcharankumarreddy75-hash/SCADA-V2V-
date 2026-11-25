#pragma once
// Neo-6M GPS driver (TinyGPSPlus NMEA parsing + UBX configuration), spec S10.4 and S8.6.
#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    bool     fix;
    uint8_t  sats;
    double   hdop;
    int32_t  lat_e7, lon_e7;
    double   speed_ms;
    double   heading_deg;      // held when speed < MIN_SPEED_FCW (1.0 m/s)
    bool     gps_valid;        // fix && sats >= GPS_MIN_SATS
    uint32_t fix_utc_ms;       // UTC time-of-day of last fix, TIMESTAMP_NONE if never
    uint32_t millis_at_fix;
    uint32_t last_fix_ms;      // millis() of last valid fix
    uint8_t  gps_quality;      // (min(sats,15)<<4) | min(round(hdop),15)
} gps_state_t;

void gps_init();                       // boot procedure S10.4 (blocks up to ~3 s, boot only)
bool gps_configure();                  // UBX CFG steps 1-4 (boot only)
void gps_poll(uint32_t now_ms);        // non-blocking: feed TinyGPSPlus, update state
const gps_state_t *gps_get();
uint32_t gps_utc_now_ms(uint32_t now_ms);  // S8.6 local_utc_ms_now, or TIMESTAMP_NONE
void gps_sim_set(const gps_state_t *s);    // SIM_MODE: inject a fake state
void ubx_checksum(const uint8_t *buf, size_t len, uint8_t *ck_a, uint8_t *ck_b);
