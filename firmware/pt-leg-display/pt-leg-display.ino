#include "ui.h"

void setup() {
  Serial.begin(115200);
  Serial.println("pt-leg-display boot");
  uiBegin();
}

void loop() {
  uiLoop();
  delay(2);
}
