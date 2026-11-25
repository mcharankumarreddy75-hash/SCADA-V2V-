#pragma once
// Battery voltage sensing (divider on PA0), spec S6.6.
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t raw;     // 12-bit ADC average
    float    vbat;    // battery volts
    uint8_t  pct;     // 0..100
    bool     low;     // vbat < VBAT_LOW
    bool     cutoff;  // vbat < VBAT_CUTOFF (radio TX off)
} power_state_t;

void power_init();
void power_read();    // call about once per second
const power_state_t *power_get();
