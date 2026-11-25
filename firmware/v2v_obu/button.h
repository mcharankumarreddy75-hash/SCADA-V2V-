#pragma once
// Push button on PB9 (INPUT_PULLUP, pressed = LOW), debounced.
#include <stdint.h>
#include <stdbool.h>

void button_init();
void button_update(uint32_t now_ms);          // call every loop
bool button_pressed();                        // debounced state
uint32_t button_held_ms(uint32_t now_ms);     // 0 if not pressed
bool button_fell();                           // true once per press (reading clears it)
