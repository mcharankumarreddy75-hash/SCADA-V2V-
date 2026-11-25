#pragma once
// Simulation mode (SIM_MODE = 1, spec S10.6). Hardware-free: feeds a fake GPS state
// into gps.cpp via gps_sim_set(). Radio, IMU, alerts and SCADA run for real.
#include <stdint.h>

// Call every loop instead of gps_poll() when SIM_MODE is 1.
// MY_VEHICLE_ID 1 is the stationary target; any other ID approaches it.
void sim_update(uint32_t now_ms);
