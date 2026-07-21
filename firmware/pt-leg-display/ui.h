#pragma once
#include <stdint.h>
#include "session.h"
#include "settings.h"

void uiBegin();
void uiLoop();
void uiSetCallbacks(void (*onStart)(), void (*onStop)(), void (*onOpenSettings)());
// linkUp false = no UART traffic from the controller recently; the screen must
// not keep presenting the last known state as if it were live.
void uiUpdateMain(bool running, Phase phase, uint32_t elapsedSec, uint16_t reps,
                  bool wifiUp, bool serverUp, bool linkUp);
void uiSetDeviceCode(const char* code);
void uiOpenSettings(const DeviceSettings& current);
void uiSetSettingsSaved(void (*onSave)(const DeviceSettings&));
// SAVE stays disabled until the controller has sent its settings at least once,
// so a screen full of placeholder values can never overwrite good NVS settings.
void uiSetSettingsKnown(bool known);
