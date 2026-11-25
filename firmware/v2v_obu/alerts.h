#pragma once
// Buzzer and LED alert patterns, spec S10.5. Non-blocking except alert_lamp_test() (boot only).
#include <stdint.h>

enum { ALERT_SOS_LOCAL = 1, ALERT_SOS_REMOTE, ALERT_FCW_CRITICAL, ALERT_EEBL, ALERT_FCW_CAUTION, ALERT_HAZARD, ALERT_NORMAL };

void alerts_init();
// duration_ms 0 = until alert_clear(). Setting an already-active pattern refreshes its
// end time but does not restart its phase (so 2-beep / blink patterns stay in step).
void alert_set(uint8_t pattern, uint32_t duration_ms, uint32_t now_ms);
void alert_clear(uint8_t pattern);
void alert_set_gps_fix(bool fix);          // NORMAL: green solid with fix, 1 Hz blink without
void alert_beep(uint32_t ms, uint32_t now_ms); // one-off buzzer override, LEDs unchanged
void alert_lamp_test();                    // boot only: all LEDs + 100 ms beep (blocking, <0.5 s)
void alert_update(uint32_t now_ms);        // call every loop
uint8_t alert_active();                    // highest-priority (lowest number) active pattern
