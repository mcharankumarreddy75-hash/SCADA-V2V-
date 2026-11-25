// Simulation mode (spec S10.6). Each board simulates its own vehicle:
//   MY_VEHICLE_ID 1 -> stationary at SIM_LAT0/SIM_LON0, heading 90 deg.
//   any other ID    -> starts SIM_START_WEST_M west of vehicle 1 at SIM_SPEED_MS,
//                      hard-stops SIM_STOP_BEHIND_M behind it, waits SIM_WAIT_MS, repeats.
// The trajectory is a pure function of millis(), so no state is kept between loops.
#include <math.h>
#include <string.h>
#include "config.h"
#include "geo.h"
#include "gps.h"
#include "sim.h"

// Use the real coordinates once they are set in config.h, else the placeholders.
#if SIM_LAT0_E7 != 0
#define SIM_LAT_USED_E7 SIM_LAT0_E7
#else
#define SIM_LAT_USED_E7 SIM_LAT0_PLACEHOLDER_E7
#endif
#if SIM_LON0_E7 != 0
#define SIM_LON_USED_E7 SIM_LON0_E7
#else
#define SIM_LON_USED_E7 SIM_LON0_PLACEHOLDER_E7
#endif

// Time for vehicle 2 to cover the approach distance (117 m at 8 m/s = 14.6 s).
#define SIM_APPROACH_MS ((uint32_t)(((SIM_START_WEST_M - SIM_STOP_BEHIND_M) / SIM_SPEED_MS) * 1000.0))
#define SIM_CYCLE_MS    (SIM_APPROACH_MS + (uint32_t)SIM_WAIT_MS)
#define SIM_HEADING_DEG 90.0
#define SIM_SATS        8
#define SIM_HDOP        1.0

void sim_update(uint32_t now_ms) {
    double east_m = 0.0;
    double speed_ms = 0.0;

    if (MY_VEHICLE_ID != 1) {
        uint32_t t = now_ms % SIM_CYCLE_MS;
        if (t < SIM_APPROACH_MS) {
            east_m = -SIM_START_WEST_M + SIM_SPEED_MS * ((double)t / 1000.0);
            speed_ms = SIM_SPEED_MS;
        } else {
            east_m = -SIM_STOP_BEHIND_M;     // hard stop, then hold until the cycle restarts
        }
    }

    // East offset in metres -> longitude offset in degrees at the simulated latitude.
    double lat0_deg = (double)SIM_LAT_USED_E7 * 1e-7;
    double dlon_deg = east_m / (EARTH_R * cos(lat0_deg * DEG2RAD)) * RAD2DEG;

    gps_state_t s;
    memset(&s, 0, sizeof s);
    s.fix = true;
    s.sats = SIM_SATS;
    s.hdop = SIM_HDOP;
    s.lat_e7 = SIM_LAT_USED_E7;
    s.lon_e7 = SIM_LON_USED_E7 + (int32_t)lround(dlon_deg * 1e7);
    s.speed_ms = speed_ms;
    s.heading_deg = SIM_HEADING_DEG;
    s.gps_valid = true;
    s.fix_utc_ms = now_ms % GPS_DAY_MS;      // fake UTC time of day from millis()
    s.millis_at_fix = now_ms;
    s.last_fix_ms = now_ms;
    s.gps_quality = (uint8_t)((SIM_SATS << 4) | (uint32_t)lround(SIM_HDOP));
    gps_sim_set(&s);
}
