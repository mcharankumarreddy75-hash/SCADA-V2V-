#include <Arduino.h>
#include "config.h"
#include "power.h"

static power_state_t p;

void power_init() {
    analogReadResolution(12);
    power_read();
}

void power_read() {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < VBAT_SAMPLES; i++) sum += analogRead(PIN_VBAT);
    p.raw = (uint16_t)(sum / VBAT_SAMPLES);
    p.vbat = (float)(p.raw / (float)ADC_MAX_COUNT * ADC_VREF_V * VBAT_DIVIDER);
    float pct = (p.vbat - VBAT_EMPTY) / (VBAT_FULL - VBAT_EMPTY) * 100.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    p.pct = (uint8_t)pct;
    p.low = p.vbat < VBAT_LOW;
    p.cutoff = p.vbat < VBAT_CUTOFF;
}

const power_state_t *power_get() {
    return &p;
}
