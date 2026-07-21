#pragma once
#include <stdint.h>

// Reads PIN_SWITCH_TOP / PIN_SWITCH_BOTTOM (INPUT_PULLUP), debounced.
// SWITCH_NC flips read polarity: NC pressed == open == HIGH; NO pressed == LOW.
void switchesBegin();
void switchesPoll(uint32_t nowMs);  // call every loop before SessionSM::tick
bool switchTop();                   // debounced logical "pressed"
bool switchBottom();
