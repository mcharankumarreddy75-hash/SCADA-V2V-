#include <Arduino.h>
#include "config.h"
#include "button.h"

static bool raw_last = false;      // last raw level (true = pressed)
static uint32_t raw_change_ms = 0;
static bool state = false;         // debounced
static uint32_t press_ms = 0;
static bool fell_flag = false;

void button_init() {
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    raw_last = (digitalRead(PIN_BUTTON) == LOW);
    state = raw_last;
    raw_change_ms = 0;
    fell_flag = false;
}

void button_update(uint32_t now_ms) {
    bool raw = (digitalRead(PIN_BUTTON) == LOW);
    if (raw != raw_last) {
        raw_last = raw;
        raw_change_ms = now_ms;
    }
    if (raw != state && (uint32_t)(now_ms - raw_change_ms) >= BUTTON_DEBOUNCE_MS) {
        state = raw;
        if (state) {
            press_ms = now_ms;
            fell_flag = true;
        }
    }
}

bool button_pressed() {
    return state;
}

uint32_t button_held_ms(uint32_t now_ms) {
    return state ? (uint32_t)(now_ms - press_ms) : 0;
}

bool button_fell() {
    bool f = fell_flag;
    fell_flag = false;
    return f;
}
