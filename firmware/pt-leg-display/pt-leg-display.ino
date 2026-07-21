#include "ui.h"

// Placeholder wiring: the UART link to the controller lands in Task 11, which
// replaces these stubs with real cmd sends and state from `link.h`.
static void onTouchStart() { Serial.println("touch start"); }
static void onTouchStop() { Serial.println("touch stop"); }
static void onOpenSettings() { Serial.println("open settings"); }

void setup() {
  Serial.begin(115200);
  Serial.println("pt-leg-display boot");
  uiBegin();
  uiSetCallbacks(onTouchStart, onTouchStop, onOpenSettings);
}

void loop() {
  uint32_t now = millis();
  uiLoop();

  static uint32_t lastUi = 0;
  if (now - lastUi >= 250) {
    lastUi = now;
    uiUpdateMain(false, Phase::Idle, 0, 0, false, false);
  }
  delay(2);
}
