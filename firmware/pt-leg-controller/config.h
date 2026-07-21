#pragma once

#define FW_VERSION "0.1.0"

#define PIN_RELAY_UP 22   // P3 connector
#define PIN_RELAY_DOWN 27 // CN1 connector
// #define RELAY_ACTIVE_LOW  // uncomment if relay board is active-low

// limit switches (controller GPIOs that support INPUT_PULLUP — NOT 34/35/36/39)
#define PIN_SWITCH_TOP 32     // closes when leg fully up
#define PIN_SWITCH_BOTTOM 33  // closes when leg fully down
// #define SWITCH_NC          // define if switches are normally-closed (fail-safe on wire break)
#define SWITCH_DEBOUNCE_MS 25

// safety cap: max time a stroke (LIFT/LOWER/SAFE-LOWER) may drive before FAULT
#define DEFAULT_MAX_TRAVEL_SEC 8

// cycle defaults (seconds) — user-adjustable in settings UI
#define DEFAULT_HOLD_SEC 10
#define DEFAULT_REST_SEC 10
