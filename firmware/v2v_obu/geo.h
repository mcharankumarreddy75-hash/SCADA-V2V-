#pragma once
// Portable geometry helpers (spec S9.2). No Arduino dependencies.
#include <stdint.h>

#define EARTH_R 6371000.0
#define DEG2RAD 0.017453292519943295
#define RAD2DEG 57.29577951308232

// Local flat-earth offset of point 2 relative to point 1, in metres.
void geo_offset(int32_t lat1_e7, int32_t lon1_e7, int32_t lat2_e7, int32_t lon2_e7,
                double *east_m, double *north_m);
double geo_distance(double e, double n);
// Compass bearing 0..360 (0 = north, 90 = east).
double geo_bearing(double e, double n);
// a - b wrapped into the range of about (-180, 180].
double angle_diff(double a, double b);
