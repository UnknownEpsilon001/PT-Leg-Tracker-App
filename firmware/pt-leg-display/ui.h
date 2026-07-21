#pragma once
#include <stdint.h>
#include "session.h"

void uiBegin();
void uiLoop();
void uiSetCallbacks(void (*onStart)(), void (*onStop)(), void (*onOpenSettings)());
void uiUpdateMain(bool running, Phase phase, uint32_t elapsedSec, uint16_t reps,
                  bool wifiUp, bool serverUp);
