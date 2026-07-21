#pragma once
#include <Arduino.h>
#include "settings.h"

void netBegin(const DeviceSettings& s);      // starts Wi-Fi (non-blocking)
void netLoop(uint32_t nowMs);                // reconnect + retry pending event
bool netWifiUp();
bool netServerUp();                          // last HTTP call succeeded
String netPollCommand();                     // "start" | "stop" | "" ; call at 1 s cadence
void netQueueEvent(const char* type, uint32_t elapsedSec, uint16_t reps); // one-slot buffer, retried until 2xx
void netHeartbeat(const char* state, uint32_t elapsedSec, uint16_t reps); // fire-and-forget
