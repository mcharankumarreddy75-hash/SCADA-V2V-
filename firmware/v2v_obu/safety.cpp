#include "safety.h"
#include "geo.h"
#include <string.h>

// Time elapsed since `then`, using unsigned subtraction (safe across millis() wraparound).
static uint32_t elapsed(uint32_t now, uint32_t then) {
    return (uint32_t)(now - then);
}

// Position of `pt` relative to `own` as distance and compass bearing.
// Returns false if either side has no GPS.
static bool relative_pos(const own_state_t *own, const v2v_packet_t *pt,
                         double *d, double *bearing) {
    if (!own->gps_valid || !(pt->flags & FLAG_GPS_VALID)) return false;
    double e, n;
    geo_offset(own->lat_e7, own->lon_e7, pt->lat_e7, pt->lon_e7, &e, &n);
    *d = geo_distance(e, n);
    *bearing = geo_bearing(e, n);
    return true;
}

// ---------------- F1 FCW ----------------

fcw_result_t fcw_evaluate(const own_state_t *own, const v2v_packet_t *nb) {
    fcw_result_t r;
    r.valid = false;
    r.level = FCW_SAFE;
    r.ttc_s = INFINITY;
    r.distance_m = INFINITY;

    double d, bearing;
    if (!relative_pos(own, nb, &d, &bearing)) return r;

    r.valid = true;
    r.distance_m = d;

    double nb_heading = nb->heading_cdeg / 100.0;
    double nb_speed = nb->speed_cms / 100.0;

    bool ahead = fabs(angle_diff(bearing, own->heading_deg)) <= AHEAD_ANGLE_DEG;
    bool same_dir = fabs(angle_diff(nb_heading, own->heading_deg)) <= SAME_DIR_ANGLE_DEG;
    if (!ahead || !same_dir || own->speed_ms < MIN_SPEED_FCW) {
        r.level = FCW_SAFE;
        return r;
    }

    double v_close = own->speed_ms -
                     nb_speed * cos(angle_diff(nb_heading, own->heading_deg) * DEG2RAD);
    double ttc = (v_close > 0.1) ? d / v_close : INFINITY;
    double headway = d / own->speed_ms;
    r.ttc_s = ttc;

    if (ttc <= TTC_CRITICAL_S) {
        r.level = FCW_CRITICAL;
    } else if (ttc <= TTC_CAUTION_S || headway < HEADWAY_MIN_S) {
        r.level = FCW_CAUTION;
    } else {
        r.level = FCW_SAFE;
    }
    return r;
}

void fcw_hyst_init(fcw_hyst_t *h) {
    memset(h, 0, sizeof *h);
}

uint8_t fcw_hyst_update(fcw_hyst_t *h, uint8_t raw_level, uint32_t now_ms) {
    if (raw_level >= h->level) {
        // Rise (or stay) immediately and cancel any pending drop.
        h->level = raw_level;
        h->has_pending = false;
        return h->level;
    }
    // raw_level is lower than the current level: start or extend the drop window.
    if (!h->has_pending) {
        h->has_pending = true;
        h->pending = raw_level;
        h->pending_since_ms = now_ms;
    } else if (raw_level > h->pending) {
        h->pending = raw_level;
    }
    if (elapsed(now_ms, h->pending_since_ms) >= (uint32_t)WARN_HOLD_MS) {
        h->level = h->pending;
        h->has_pending = false;
    }
    return h->level;
}

// ---------------- F2 hard braking ----------------

void brake_init(brake_det_t *d) {
    memset(d, 0, sizeof *d);
}

bool brake_update(brake_det_t *d, double a_long, double speed_ms, bool gps_valid, uint32_t now_ms) {
    bool cond = (a_long < HARD_BRAKE_MS2) &&
                (speed_ms > HARD_BRAKE_MIN_SPEED_MS || !gps_valid);
    if (!cond) {
        d->cond_active = false;
        return false;
    }
    if (!d->cond_active) {
        d->cond_active = true;
        d->cond_start_ms = now_ms;
        return false;
    }
    if (elapsed(now_ms, d->cond_start_ms) < (uint32_t)HARD_BRAKE_TIME_MS) return false;
    if (d->has_fired && elapsed(now_ms, d->fired_ms) < (uint32_t)HARD_BRAKE_LOCKOUT_MS) return false;

    d->has_fired = true;
    d->fired_ms = now_ms;
    return true;
}

bool brake_flag_active(const brake_det_t *d, uint32_t now_ms) {
    return d->has_fired && elapsed(now_ms, d->fired_ms) < (uint32_t)FLAG_HOLD_MS;
}

// ---------------- F3 crash ----------------

void crash_init(crash_det_t *d) {
    memset(d, 0, sizeof *d);
}

bool crash_update(crash_det_t *d, double a_total_g, double speed_ms, bool gps_valid, uint32_t now_ms) {
    if (d->sos_fired) return false;

    if (!d->candidate && a_total_g > CRASH_G) {
        d->candidate = true;
        d->t0_ms = now_ms;
    }
    if (!d->candidate) return false;

    if (elapsed(now_ms, d->t0_ms) <= (uint32_t)CRASH_CONFIRM_MS) {
        bool stopped = (gps_valid && speed_ms < MIN_SPEED_CRASH_MS) || !gps_valid;
        if (stopped) {
            d->candidate = false;
            d->sos_fired = true;
            return true;
        }
    } else {
        // Confirmation window passed without a stop: not a crash.
        d->candidate = false;
    }
    return false;
}

// ---------------- F4 rollover ----------------

void rollover_init(rollover_det_t *d) {
    memset(d, 0, sizeof *d);
}

bool rollover_update(rollover_det_t *d, double roll_deg, double pitch_deg, uint32_t now_ms) {
    bool tilted = fabs(roll_deg) > ROLLOVER_DEG || fabs(pitch_deg) > ROLLOVER_DEG;
    if (!tilted) {
        // Re-arm once both axes are back within the limit.
        d->over = false;
        d->fired = false;
        return false;
    }
    if (!d->over) {
        d->over = true;
        d->over_since_ms = now_ms;
    }
    if (d->fired) return false;
    if (elapsed(now_ms, d->over_since_ms) >= (uint32_t)ROLLOVER_TIME_MS) {
        d->fired = true;
        return true;
    }
    return false;
}

// ---------------- F5 pothole ----------------

void pothole_init(pothole_det_t *d) {
    memset(d, 0, sizeof *d);
}

bool pothole_update(pothole_det_t *d, double az_hp, double speed_ms, bool gps_valid, uint32_t now_ms) {
    bool hit = (fabs(az_hp) > POTHOLE_G * GRAVITY_MS2) &&
               (speed_ms > POTHOLE_MIN_SPEED_MS) && gps_valid;
    if (!hit) return false;
    if (d->has_fired && elapsed(now_ms, d->fired_ms) < (uint32_t)POTHOLE_LOCKOUT_MS) return false;
    d->has_fired = true;
    d->fired_ms = now_ms;
    return true;
}

bool pothole_flag_active(const pothole_det_t *d, uint32_t now_ms) {
    return d->has_fired && elapsed(now_ms, d->fired_ms) < (uint32_t)FLAG_HOLD_MS;
}

// ---------------- Receive-side filters ----------------

bool should_alert_eebl(const own_state_t *own, const v2v_packet_t *sender) {
    double d, bearing;
    if (!relative_pos(own, sender, &d, &bearing)) return true;   // no GPS: alert anyway
    bool ahead = fabs(angle_diff(bearing, own->heading_deg)) <= EEBL_AHEAD_DEG;
    return ahead && d <= EEBL_RANGE_M;
}

bool should_alert_hazard(const own_state_t *own, const v2v_packet_t *hazard) {
    double d, bearing;
    if (!relative_pos(own, hazard, &d, &bearing)) return false;  // needs both GPS
    bool ahead = fabs(angle_diff(bearing, own->heading_deg)) <= HAZARD_AHEAD_DEG;
    return ahead && d <= HAZARD_RANGE_M;
}
