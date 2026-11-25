#include "test.h"
#include "geo.h"
#include <cmath>

TEST(geo_offset_zero_for_same_point) {
    double e = 99, n = 99;
    geo_offset(120000000, 770000000, 120000000, 770000000, &e, &n);
    CHECK_NEAR(e, 0.0, 1e-9);
    CHECK_NEAR(n, 0.0, 1e-9);
}

TEST(geo_one_km_distance_within_half_percent) {
    // 1000 m north of (12.0, 77.0): dlat = 1000 / R in degrees.
    int32_t lat2 = 120000000 + (int32_t)std::lround(1000.0 / EARTH_R * RAD2DEG * 1e7);
    double e, n;
    geo_offset(120000000, 770000000, lat2, 770000000, &e, &n);
    double d = geo_distance(e, n);
    CHECK_NEAR(d, 1000.0, 1000.0 * 0.005);
    CHECK_NEAR(e, 0.0, 1e-9);
}

TEST(geo_real_coordinates_about_1km) {
    // Two points 0.009 deg of latitude apart (about 1001 m).
    double e, n;
    geo_offset(129716000, 775946000, 129806000, 775946000, &e, &n);
    double d = geo_distance(e, n);
    CHECK_NEAR(d, 1000.0, 1000.0 * 0.005);
}

TEST(geo_bearings_0_90_180_270) {
    CHECK_NEAR(geo_bearing(0.0, 100.0), 0.0, 1e-9);     // north
    CHECK_NEAR(geo_bearing(100.0, 0.0), 90.0, 1e-9);    // east
    CHECK_NEAR(geo_bearing(0.0, -100.0), 180.0, 1e-9);  // south
    CHECK_NEAR(geo_bearing(-100.0, 0.0), 270.0, 1e-9);  // west
}

TEST(geo_bearings_from_coordinates) {
    double e, n;
    // Same latitude, 0.001 deg further east -> bearing 90.
    geo_offset(120000000, 770000000, 120000000, 770010000, &e, &n);
    CHECK_NEAR(geo_bearing(e, n), 90.0, 1e-6);
    // 0.001 deg further north -> bearing 0.
    geo_offset(120000000, 770000000, 120010000, 770000000, &e, &n);
    CHECK_NEAR(geo_bearing(e, n), 0.0, 1e-6);
    // 0.001 deg further west -> bearing 270.
    geo_offset(120000000, 770000000, 120000000, 769990000, &e, &n);
    CHECK_NEAR(geo_bearing(e, n), 270.0, 1e-6);
}

TEST(geo_distance_pythagoras) {
    CHECK_NEAR(geo_distance(3.0, 4.0), 5.0, 1e-12);
}

TEST(angle_diff_wraparound) {
    CHECK_NEAR(angle_diff(350.0, 10.0), -20.0, 1e-9);
    CHECK_NEAR(angle_diff(10.0, 350.0), 20.0, 1e-9);
    CHECK_NEAR(angle_diff(90.0, 90.0), 0.0, 1e-9);
    CHECK_NEAR(angle_diff(90.0, 45.0), 45.0, 1e-9);
    CHECK_NEAR(angle_diff(200.0, 10.0), -170.0, 1e-9);
}
