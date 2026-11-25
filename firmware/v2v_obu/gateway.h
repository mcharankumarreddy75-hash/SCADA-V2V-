#pragma once
// Phase 6 gateway (spec S11): Modbus RTU slave on USB Serial, registers 0-109.
// The gw_* prototypes live in app_state.h (the shared contract), compiled only when IS_GATEWAY = 1.
// gateway.cpp is compiled as part of every build (Arduino builds all .cpp files) but is empty unless IS_GATEWAY = 1.
#include "app_state.h"
