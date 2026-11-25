#include "geo.h"
#include <math.h>

void geo_offset(int32_t lat1_e7, int32_t lon1_e7, int32_t lat2_e7, int32_t lon2_e7,
                double *east_m, double *north_m) {
    double lat1 = lat1_e7 * 1e-7 * DEG2RAD;
    double lat2 = lat2_e7 * 1e-7 * DEG2RAD;
    double dlat = (double)(lat2_e7 - lat1_e7) * 1e-7 * DEG2RAD;
    double dlon = (double)(lon2_e7 - lon1_e7) * 1e-7 * DEG2RAD;
    *east_m  = EARTH_R * dlon * cos((lat1 + lat2) / 2.0);
    *north_m = EARTH_R * dlat;
}

double geo_distance(double e, double n) { return sqrt(e * e + n * n); }

double geo_bearing(double e, double n) {
    double b = atan2(e, n) * RAD2DEG;
    return b < 0 ? b + 360.0 : b;
}

double angle_diff(double a, double b) { return fmod(a - b + 540.0, 360.0) - 180.0; }
