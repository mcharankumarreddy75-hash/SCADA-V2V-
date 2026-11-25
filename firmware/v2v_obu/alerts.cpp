#include <Arduino.h>
#include "config.h"
#include "alerts.h"

typedef struct {
    bool     active;
    bool     forever;
    uint32_t start_ms;   // phase reference
    uint32_t end_ms;     // valid when !forever
} slot_t;

static slot_t slots[ALERT_NUM_PATTERNS + 1];   // index 1..7 (0 unused)
static bool gps_fix = false;
static uint32_t beep_end_ms = 0;               // one-off beep override
static uint8_t current = ALERT_NORMAL;

// True while phase ph (ms) lies inside one of `count` beeps of beep_ms, spaced beep_ms+gap_ms.
static bool in_beeps(uint32_t ph, uint32_t beep_ms, uint32_t gap_ms, uint32_t count) {
    uint32_t cyc = beep_ms + gap_ms;
    uint32_t idx = ph / cyc;
    if (idx >= count) return false;
    return (ph % cyc) < beep_ms;
}

// Outputs for pattern p at phase ph (ms since the pattern started).
static void pattern_out(uint8_t p, uint32_t ph, bool *buz, bool *red, bool *yel, bool *grn) {
    *buz = false; *red = false; *yel = false; *grn = false;
    switch (p) {
        case ALERT_SOS_LOCAL:
            *buz = (ph % ALERT_SOS_LOCAL_PERIOD_MS) < (ALERT_SOS_LOCAL_PERIOD_MS / 2);
            *red = (ph % (2 * ALERT_SOS_LOCAL_LED_MS)) < ALERT_SOS_LOCAL_LED_MS;
            break;
        case ALERT_SOS_REMOTE:
            *buz = in_beeps(ph % ALERT_SOS_REMOTE_PERIOD_MS, ALERT_SOS_REMOTE_BEEP_MS,
                            ALERT_SOS_REMOTE_BEEP_MS, ALERT_SOS_REMOTE_COUNT);
            *red = (ph % (2 * ALERT_SOS_REMOTE_LED_MS)) < ALERT_SOS_REMOTE_LED_MS;
            break;
        case ALERT_FCW_CRITICAL:
            *buz = true;
            *red = true;
            break;
        case ALERT_EEBL:
            *buz = (ph % ALERT_EEBL_PERIOD_MS) < (ALERT_EEBL_PERIOD_MS / 2);
            *red = *buz;
            break;
        case ALERT_FCW_CAUTION:
            *buz = (ph % ALERT_CAUTION_PERIOD_MS) < ALERT_CAUTION_BEEP_MS;
            *yel = true;
            break;
        case ALERT_HAZARD:
            *buz = in_beeps(ph, ALERT_HAZ_BEEP_MS, ALERT_HAZ_GAP_MS, ALERT_HAZ_COUNT);
            *yel = (ph % ALERT_HAZ_LED_PERIOD_MS) < (ALERT_HAZ_LED_PERIOD_MS / 2);
            break;
        default:   // ALERT_NORMAL
            *grn = gps_fix || ((ph % (2 * ALERT_NORMAL_BLINK_MS)) < ALERT_NORMAL_BLINK_MS);
            break;
    }
}

static void write_outputs(bool buz, bool red, bool yel, bool grn) {
    digitalWrite(PIN_BUZZER, buz ? ALERT_OUT_ON : ALERT_OUT_OFF);
    digitalWrite(PIN_LED_RED, red ? ALERT_OUT_ON : ALERT_OUT_OFF);
    digitalWrite(PIN_LED_YEL, yel ? ALERT_OUT_ON : ALERT_OUT_OFF);
    digitalWrite(PIN_LED_GRN, grn ? ALERT_OUT_ON : ALERT_OUT_OFF);
}

void alerts_init() {
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_LED_RED, OUTPUT);
    pinMode(PIN_LED_YEL, OUTPUT);
    pinMode(PIN_LED_GRN, OUTPUT);
    write_outputs(false, false, false, false);
    for (uint8_t p = 0; p <= ALERT_NUM_PATTERNS; p++) {
        slots[p].active = false;
        slots[p].forever = false;
        slots[p].start_ms = 0;
        slots[p].end_ms = 0;
    }
    slots[ALERT_NORMAL].active = true;      // NORMAL is always the fallback
    slots[ALERT_NORMAL].forever = true;
    beep_end_ms = 0;
    current = ALERT_NORMAL;
    gps_fix = false;
}

void alert_set(uint8_t pattern, uint32_t duration_ms, uint32_t now_ms) {
    if (pattern < ALERT_SOS_LOCAL || pattern >= ALERT_NORMAL) return;   // NORMAL is not settable
    slot_t *s = &slots[pattern];
    if (!s->active) {
        s->active = true;
        s->start_ms = now_ms;
    }
    s->forever = (duration_ms == 0);
    s->end_ms = now_ms + duration_ms;
}

void alert_clear(uint8_t pattern) {
    if (pattern < ALERT_SOS_LOCAL || pattern >= ALERT_NORMAL) return;
    slots[pattern].active = false;
}

void alert_set_gps_fix(bool fix) {
    gps_fix = fix;
}

void alert_beep(uint32_t ms, uint32_t now_ms) {
    beep_end_ms = now_ms + ms;
}

void alert_lamp_test() {
    // S10.5: lamp test 0.5 s (all LEDs) + 100 ms beep at the start. Boot only (blocking is allowed).
    write_outputs(true, true, true, true);
    delay(ALERT_LAMP_TEST_MS);
    write_outputs(false, true, true, true);
    delay(ALERT_LAMP_TEST_LED_MS - ALERT_LAMP_TEST_MS);
    write_outputs(false, false, false, false);
}

void alert_update(uint32_t now_ms) {
    // Expire timed patterns.
    for (uint8_t p = ALERT_SOS_LOCAL; p < ALERT_NORMAL; p++) {
        if (slots[p].active && !slots[p].forever && (int32_t)(now_ms - slots[p].end_ms) >= 0) {
            slots[p].active = false;
        }
    }
    // Highest priority = lowest number among active.
    uint8_t top = ALERT_NORMAL;
    for (uint8_t p = ALERT_SOS_LOCAL; p <= ALERT_NORMAL; p++) {
        if (slots[p].active) { top = p; break; }
    }
    current = top;

    uint32_t ph = (top == ALERT_NORMAL) ? now_ms : (uint32_t)(now_ms - slots[top].start_ms);
    bool buz, red, yel, grn;
    pattern_out(top, ph, &buz, &red, &yel, &grn);
    if ((int32_t)(beep_end_ms - now_ms) > 0) buz = true;
    write_outputs(buz, red, yel, grn);
}

uint8_t alert_active() {
    return current;
}
