#pragma once
// Portable safety logic (spec S9.3 - S9.7). No Arduino dependencies.
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "config.h"
#include "v2v_packet.h"

// ---- Constants not in config.h (named here, no magic numbers in safety.cpp) ----
#ifndef GRAVITY_MS2
#define GRAVITY_MS2 9.81
#endif
#ifndef FLAG_HOLD_MS
#define FLAG_HOLD_MS 2000            // SOS/hazard/EEBL flag stays set this long after firing
#endif
#ifndef HARD_BRAKE_LOCKOUT_MS
#define HARD_BRAKE_LOCKOUT_MS 2000   // no new EEBL event this long after firing
#endif
#ifndef HARD_BRAKE_MIN_SPEED_MS
#define HARD_BRAKE_MIN_SPEED_MS 2.0  // braking only counts above this speed when GPS is valid
#endif
#ifndef MIN_SPEED_CRASH_MS
#define MIN_SPEED_CRASH_MS 1.0       // "stopped" for crash confirmation (S9.5)
#endif
#ifndef POTHOLE_LOCKOUT_MS
#define POTHOLE_LOCKOUT_MS 3000
#endif
#ifndef POTHOLE_MIN_SPEED_MS
#define POTHOLE_MIN_SPEED_MS 3.0
#endif

// ---- Own state (inputs to the safety functions) ----
typedef struct {
    int32_t lat_e7, lon_e7;
    double  speed_ms;
    double  heading_deg;
    bool    gps_valid;
} own_state_t;

// ---- F1 forward collision warning ----
enum { FCW_SAFE = 0, FCW_CAUTION = 1, FCW_CRITICAL = 2 };

typedef struct {
    bool    valid;        // false = level NONE (no GPS on one side); level is then SAFE
    uint8_t level;
    double  ttc_s;        // INFINITY if none
    double  distance_m;   // INFINITY if no GPS on one side
} fcw_result_t;

// Evaluates one neighbour (S9.3). Caller takes the max level / min TTC over neighbours.
fcw_result_t fcw_evaluate(const own_state_t *own, const v2v_packet_t *nb);

// Hysteresis: rises immediately; falls only after a lower level has held WARN_HOLD_MS.
typedef struct {
    uint8_t  level;
    uint8_t  pending;          // highest lower raw level seen since the drop started
    bool     has_pending;
    uint32_t pending_since_ms;
} fcw_hyst_t;

void    fcw_hyst_init(fcw_hyst_t *h);
uint8_t fcw_hyst_update(fcw_hyst_t *h, uint8_t raw_level, uint32_t now_ms);

// ---- F2 hard braking (S9.4) ----
typedef struct {
    bool     cond_active;
    uint32_t cond_start_ms;
    bool     has_fired;
    uint32_t fired_ms;
} brake_det_t;

void brake_init(brake_det_t *d);
bool brake_update(brake_det_t *d, double a_long, double speed_ms, bool gps_valid, uint32_t now_ms);
bool brake_flag_active(const brake_det_t *d, uint32_t now_ms);

// ---- F3 crash (S9.5) ----
typedef struct {
    bool     candidate;
    uint32_t t0_ms;
    bool     sos_fired;
} crash_det_t;

void crash_init(crash_det_t *d);
bool crash_update(crash_det_t *d, double a_total_g, double speed_ms, bool gps_valid, uint32_t now_ms);

// ---- F4 rollover (S9.6) ----
typedef struct {
    bool     over;
    uint32_t over_since_ms;
    bool     fired;
} rollover_det_t;

void rollover_init(rollover_det_t *d);
bool rollover_update(rollover_det_t *d, double roll_deg, double pitch_deg, uint32_t now_ms);

// ---- F5 pothole (S9.7) ----
typedef struct {
    bool     has_fired;
    uint32_t fired_ms;
} pothole_det_t;

void pothole_init(pothole_det_t *d);
bool pothole_update(pothole_det_t *d, double az_hp, double speed_ms, bool gps_valid, uint32_t now_ms);
bool pothole_flag_active(const pothole_det_t *d, uint32_t now_ms);

// ---- Receive-side filters ----
bool should_alert_eebl(const own_state_t *own, const v2v_packet_t *sender);
bool should_alert_hazard(const own_state_t *own, const v2v_packet_t *hazard);
