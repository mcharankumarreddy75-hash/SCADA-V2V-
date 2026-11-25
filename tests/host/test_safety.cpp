#include "test.h"
#include "safety.h"
#include "geo.h"
#include <cmath>
#include <cstring>

static const int32_t LAT0_E7 = 120000000;   // 12.0 deg
static const int32_t LON0_E7 = 770000000;   // 77.0 deg

// Packet for a neighbour placed `east`/`north` metres from the origin.
static v2v_packet_t nb_pkt(double east, double north, double heading_deg, double speed_ms,
                           bool gps = true) {
    v2v_packet_t p;
    memset(&p, 0, sizeof p);
    p.type = PKT_BSM;
    p.vehicle_id = 2;
    p.lat_e7 = LAT0_E7 + (int32_t)std::lround(north / EARTH_R * RAD2DEG * 1e7);
    p.lon_e7 = LON0_E7 + (int32_t)std::lround(east / (EARTH_R * std::cos(12.0 * DEG2RAD)) * RAD2DEG * 1e7);
    p.heading_cdeg = (uint16_t)std::lround(heading_deg * 100.0);
    p.speed_cms = (uint16_t)std::lround(speed_ms * 100.0);
    p.flags = gps ? FLAG_GPS_VALID : 0;
    return p;
}

static own_state_t own_at(double speed_ms, double heading_deg, bool gps = true) {
    own_state_t o;
    o.lat_e7 = LAT0_E7;
    o.lon_e7 = LON0_E7;
    o.speed_ms = speed_ms;
    o.heading_deg = heading_deg;
    o.gps_valid = gps;
    return o;
}

// ---------------- F1 FCW ----------------

TEST(fcw_worked_example_safe_caution_critical) {
    own_state_t own = own_at(15.0, 90.0);
    // 40 m due east, 10 m/s heading 90 -> TTC 8 s, headway 2.7 s -> SAFE.
    v2v_packet_t p = nb_pkt(40.0, 0.0, 90.0, 10.0);
    fcw_result_t r = fcw_evaluate(&own, &p);
    CHECK(r.valid);
    CHECK_NEAR(r.distance_m, 40.0, 0.05);
    CHECK_NEAR(r.ttc_s, 8.0, 0.05);
    CHECK_EQ(r.level, FCW_SAFE);

    // 15 m -> TTC 3 s -> CAUTION.
    p = nb_pkt(15.0, 0.0, 90.0, 10.0);
    r = fcw_evaluate(&own, &p);
    CHECK_NEAR(r.ttc_s, 3.0, 0.05);
    CHECK_EQ(r.level, FCW_CAUTION);

    // 9 m -> TTC 1.8 s -> CRITICAL.
    p = nb_pkt(9.0, 0.0, 90.0, 10.0);
    r = fcw_evaluate(&own, &p);
    CHECK_NEAR(r.ttc_s, 1.8, 0.05);
    CHECK_EQ(r.level, FCW_CRITICAL);
}

TEST(fcw_headway_rule_gives_caution) {
    // Slow lead vehicle far ahead: TTC is infinite (we are slower) but headway 0.6 s -> CAUTION.
    own_state_t own = own_at(10.0, 90.0);
    v2v_packet_t p = nb_pkt(6.0, 0.0, 90.0, 20.0);   // faster than us -> v_close <= 0
    fcw_result_t r = fcw_evaluate(&own, &p);
    CHECK(std::isinf(r.ttc_s));
    CHECK_EQ(r.level, FCW_CAUTION);
}

TEST(fcw_vehicle_behind_is_safe) {
    own_state_t own = own_at(15.0, 90.0);
    v2v_packet_t p = nb_pkt(-5.0, 0.0, 90.0, 10.0);   // 5 m directly behind
    fcw_result_t r = fcw_evaluate(&own, &p);
    CHECK(r.valid);
    CHECK_NEAR(r.distance_m, 5.0, 0.05);
    CHECK_EQ(r.level, FCW_SAFE);
}

TEST(fcw_oncoming_vehicle_is_safe) {
    own_state_t own = own_at(15.0, 90.0);
    v2v_packet_t p = nb_pkt(9.0, 0.0, 270.0, 15.0);   // 9 m ahead but heading toward us
    fcw_result_t r = fcw_evaluate(&own, &p);
    CHECK_EQ(r.level, FCW_SAFE);
}

TEST(fcw_own_speed_below_1ms_is_safe) {
    own_state_t own = own_at(0.5, 90.0);
    v2v_packet_t p = nb_pkt(1.0, 0.0, 90.0, 0.0);     // would be CRITICAL without the speed gate
    fcw_result_t r = fcw_evaluate(&own, &p);
    CHECK_EQ(r.level, FCW_SAFE);
}

TEST(fcw_no_gps_gives_invalid_level_none) {
    own_state_t own = own_at(15.0, 90.0, false);
    v2v_packet_t p = nb_pkt(9.0, 0.0, 90.0, 10.0);
    fcw_result_t r = fcw_evaluate(&own, &p);
    CHECK(!r.valid);
    CHECK_EQ(r.level, FCW_SAFE);
    CHECK(std::isinf(r.distance_m));

    own = own_at(15.0, 90.0, true);
    p = nb_pkt(9.0, 0.0, 90.0, 10.0, false);          // neighbour without GPS
    r = fcw_evaluate(&own, &p);
    CHECK(!r.valid);
}

TEST(fcw_hysteresis_falls_only_after_1s) {
    fcw_hyst_t h;
    fcw_hyst_init(&h);
    CHECK_EQ(fcw_hyst_update(&h, FCW_CRITICAL, 0), FCW_CRITICAL);   // rises immediately
    CHECK_EQ(fcw_hyst_update(&h, FCW_SAFE, 100), FCW_CRITICAL);     // drop starts
    CHECK_EQ(fcw_hyst_update(&h, FCW_SAFE, 1000), FCW_CRITICAL);    // only 900 ms held
    CHECK_EQ(fcw_hyst_update(&h, FCW_SAFE, 1100), FCW_SAFE);        // 1000 ms held -> drops
}

TEST(fcw_hysteresis_rise_cancels_pending_drop) {
    fcw_hyst_t h;
    fcw_hyst_init(&h);
    CHECK_EQ(fcw_hyst_update(&h, FCW_CAUTION, 0), FCW_CAUTION);
    CHECK_EQ(fcw_hyst_update(&h, FCW_SAFE, 100), FCW_CAUTION);
    CHECK_EQ(fcw_hyst_update(&h, FCW_CRITICAL, 500), FCW_CRITICAL); // rise, pending cancelled
    CHECK_EQ(fcw_hyst_update(&h, FCW_SAFE, 600), FCW_CRITICAL);     // new drop window starts
    CHECK_EQ(fcw_hyst_update(&h, FCW_SAFE, 1500), FCW_CRITICAL);
    CHECK_EQ(fcw_hyst_update(&h, FCW_SAFE, 1600), FCW_SAFE);
}

// ---------------- F2 hard braking ----------------

TEST(brake_fires_after_200ms_sustained) {
    brake_det_t d;
    brake_init(&d);
    int fires = 0;
    uint32_t fire_t = 0;
    for (uint32_t t = 1000; t <= 1300; t += 10) {
        if (brake_update(&d, -5.0, 10.0, true, t)) { fires++; fire_t = t; }
    }
    CHECK_EQ(fires, 1);
    CHECK_EQ(fire_t, 1200);
}

TEST(brake_ignores_short_spike) {
    brake_det_t d;
    brake_init(&d);
    int fires = 0;
    for (uint32_t t = 1000; t < 1190; t += 10) if (brake_update(&d, -5.0, 10.0, true, t)) fires++;
    for (uint32_t t = 1190; t <= 1600; t += 10) if (brake_update(&d, 0.0, 10.0, true, t)) fires++;
    CHECK_EQ(fires, 0);
}

TEST(brake_lockout_2s_after_firing) {
    brake_det_t d;
    brake_init(&d);
    int fires_in_lockout = 0, fires_total = 0;
    uint32_t second_fire = 0;
    for (uint32_t t = 1000; t <= 3300; t += 10) {
        if (brake_update(&d, -5.0, 10.0, true, t)) {
            fires_total++;
            if (t > 1200) { second_fire = t; }
            if (t > 1200 && t < 3200) fires_in_lockout++;
        }
    }
    CHECK_EQ(fires_in_lockout, 0);
    CHECK_EQ(fires_total, 2);      // 1200 and again at 3200 (2 s lockout ended)
    CHECK_EQ(second_fire, 3200);
}

TEST(brake_needs_speed_or_no_gps) {
    brake_det_t d;
    brake_init(&d);
    int fires = 0;
    for (uint32_t t = 0; t <= 500; t += 10) if (brake_update(&d, -5.0, 1.5, true, t)) fires++;
    CHECK_EQ(fires, 0);   // speed <= 2 m/s with GPS: not a braking event
    brake_init(&d);
    for (uint32_t t = 0; t <= 500; t += 10) if (brake_update(&d, -5.0, 0.0, false, t)) fires++;
    CHECK_EQ(fires, 1);   // no GPS: fires regardless of speed
}

TEST(brake_flag_active_for_2s) {
    brake_det_t d;
    brake_init(&d);
    for (uint32_t t = 1000; t <= 1200; t += 10) brake_update(&d, -5.0, 10.0, true, t);
    CHECK(brake_flag_active(&d, 1200));
    CHECK(brake_flag_active(&d, 3199));
    CHECK(!brake_flag_active(&d, 3200));
}

// ---------------- F3 crash ----------------

TEST(crash_fires_when_stopped_within_3s) {
    crash_det_t d;
    crash_init(&d);
    int fires = 0;
    CHECK(!crash_update(&d, 5.0, 10.0, true, 0));       // candidate starts
    for (uint32_t t = 10; t < 1000; t += 10) if (crash_update(&d, 0.0, 10.0, true, t)) fires++;
    CHECK_EQ(fires, 0);
    if (crash_update(&d, 0.0, 0.5, true, 1000)) fires++;  // stopped at 1 s
    CHECK_EQ(fires, 1);
    CHECK(!crash_update(&d, 0.0, 0.0, true, 1010));      // never fires twice
}

TEST(crash_no_fire_when_still_moving_after_3s) {
    crash_det_t d;
    crash_init(&d);
    int fires = 0;
    CHECK(!crash_update(&d, 5.0, 10.0, true, 0));
    for (uint32_t t = 10; t <= 3100; t += 10) if (crash_update(&d, 0.0, 10.0, true, t)) fires++;
    CHECK_EQ(fires, 0);
    CHECK(!crash_update(&d, 0.0, 0.5, true, 3200));       // window already cleared
}

TEST(crash_no_gps_fires_immediately) {
    crash_det_t d;
    crash_init(&d);
    CHECK(crash_update(&d, 5.0, 10.0, false, 0));
}

TEST(crash_below_threshold_no_candidate) {
    crash_det_t d;
    crash_init(&d);
    CHECK(!crash_update(&d, 3.9, 0.0, true, 0));
    CHECK(!crash_update(&d, 3.9, 0.0, true, 100));
    CHECK(!crash_update(&d, 3.9, 0.0, false, 200));
}

TEST(crash_latched_until_reinit) {
    crash_det_t d;
    crash_init(&d);
    CHECK(crash_update(&d, 5.0, 0.0, true, 0));
    CHECK(!crash_update(&d, 5.0, 0.0, true, 5000));
    crash_init(&d);
    CHECK(crash_update(&d, 5.0, 0.0, true, 6000));
}

// ---------------- F4 rollover ----------------

TEST(rollover_fires_after_1s_tilt) {
    rollover_det_t d;
    rollover_init(&d);
    int fires = 0;
    uint32_t fire_t = 0;
    for (uint32_t t = 0; t <= 1500; t += 10) {
        if (rollover_update(&d, 70.0, 0.0, t)) { fires++; fire_t = t; }
    }
    CHECK_EQ(fires, 1);
    CHECK_EQ(fire_t, 1000);
}

TEST(rollover_ignores_short_tilt_and_small_angles) {
    rollover_det_t d;
    rollover_init(&d);
    int fires = 0;
    for (uint32_t t = 0; t < 900; t += 10) if (rollover_update(&d, 70.0, 0.0, t)) fires++;
    for (uint32_t t = 900; t <= 3000; t += 10) if (rollover_update(&d, 0.0, 0.0, t)) fires++;
    CHECK_EQ(fires, 0);
    rollover_init(&d);
    for (uint32_t t = 0; t <= 3000; t += 10) if (rollover_update(&d, 59.0, -59.0, t)) fires++;
    CHECK_EQ(fires, 0);
}

TEST(rollover_pitch_and_rearm) {
    rollover_det_t d;
    rollover_init(&d);
    int fires = 0;
    for (uint32_t t = 0; t <= 1200; t += 10) if (rollover_update(&d, 0.0, -65.0, t)) fires++;
    CHECK_EQ(fires, 1);
    for (uint32_t t = 1210; t <= 1500; t += 10) if (rollover_update(&d, 0.0, 0.0, t)) fires++;
    for (uint32_t t = 1600; t <= 2600; t += 10) if (rollover_update(&d, 70.0, 0.0, t)) fires++;
    CHECK_EQ(fires, 2);   // re-armed after returning within 60 deg
}

// ---------------- F5 pothole ----------------

TEST(pothole_fires_above_1_5g_and_respects_lockout) {
    pothole_det_t d;
    pothole_init(&d);
    CHECK(pothole_update(&d, 15.0, 5.0, true, 0));
    CHECK(!pothole_update(&d, 15.0, 5.0, true, 100));
    CHECK(!pothole_update(&d, 15.0, 5.0, true, 2999));
    CHECK(pothole_update(&d, 15.0, 5.0, true, 3000));     // 3 s lockout over
}

TEST(pothole_no_fire_below_threshold_speed_or_gps) {
    pothole_det_t d;
    pothole_init(&d);
    CHECK(!pothole_update(&d, 14.0, 5.0, true, 0));       // below 1.5 g
    CHECK(!pothole_update(&d, 15.0, 2.0, true, 10));      // too slow
    CHECK(!pothole_update(&d, 15.0, 5.0, false, 20));     // no GPS
    CHECK(!pothole_update(&d, -14.0, 5.0, true, 30));
}

TEST(pothole_flag_active_for_2s) {
    pothole_det_t d;
    pothole_init(&d);
    CHECK(pothole_update(&d, 20.0, 5.0, true, 1000));
    CHECK(pothole_flag_active(&d, 2999));
    CHECK(!pothole_flag_active(&d, 3000));
}

// ---------------- Receive-side filters ----------------

TEST(eebl_filter_ahead_in_range_alerts) {
    own_state_t own = own_at(15.0, 90.0);
    v2v_packet_t ahead = nb_pkt(100.0, 0.0, 90.0, 10.0);
    CHECK(should_alert_eebl(&own, &ahead));
    v2v_packet_t behind = nb_pkt(-100.0, 0.0, 90.0, 10.0);
    CHECK(!should_alert_eebl(&own, &behind));
    v2v_packet_t far = nb_pkt(400.0, 0.0, 90.0, 10.0);
    CHECK(!should_alert_eebl(&own, &far));                 // beyond 300 m
    v2v_packet_t side = nb_pkt(100.0, 40.0, 90.0, 10.0);   // bearing ~68 deg from north = 22 off heading
    CHECK(should_alert_eebl(&own, &side));
    v2v_packet_t wide = nb_pkt(40.0, 100.0, 90.0, 10.0);   // bearing ~22 deg from north = 68 off heading
    CHECK(!should_alert_eebl(&own, &wide));
}

TEST(eebl_filter_without_gps_alerts_anyway) {
    own_state_t own = own_at(15.0, 90.0);
    v2v_packet_t nogps = nb_pkt(-100.0, 0.0, 90.0, 10.0, false);
    CHECK(should_alert_eebl(&own, &nogps));
    own_state_t own_nogps = own_at(15.0, 90.0, false);
    v2v_packet_t behind = nb_pkt(-100.0, 0.0, 90.0, 10.0);
    CHECK(should_alert_eebl(&own_nogps, &behind));
}

TEST(hazard_filter_ahead_within_200m) {
    own_state_t own = own_at(15.0, 90.0);
    v2v_packet_t ahead = nb_pkt(150.0, 0.0, 0.0, 0.0);
    CHECK(should_alert_hazard(&own, &ahead));
    v2v_packet_t far = nb_pkt(250.0, 0.0, 0.0, 0.0);
    CHECK(!should_alert_hazard(&own, &far));
    v2v_packet_t off_axis = nb_pkt(40.0, 100.0, 0.0, 0.0);  // bearing ~22 deg, 68 off our heading 90
    CHECK(!should_alert_hazard(&own, &off_axis));
    v2v_packet_t behind = nb_pkt(-100.0, 0.0, 0.0, 0.0);
    CHECK(!should_alert_hazard(&own, &behind));
}

TEST(hazard_filter_needs_both_gps) {
    v2v_packet_t ahead = nb_pkt(150.0, 0.0, 0.0, 0.0);
    own_state_t own_nogps = own_at(15.0, 90.0, false);
    CHECK(!should_alert_hazard(&own_nogps, &ahead));
    own_state_t own = own_at(15.0, 90.0);
    v2v_packet_t nogps = nb_pkt(150.0, 0.0, 0.0, 0.0, false);
    CHECK(!should_alert_hazard(&own, &nogps));
}
