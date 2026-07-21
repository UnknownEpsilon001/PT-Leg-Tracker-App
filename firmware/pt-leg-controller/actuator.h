#pragma once
#include <stdint.h>

enum ActState { ACT_OFF, ACT_UP, ACT_DOWN };

// Safety invariants enforced here and nowhere else:
// - UP and DOWN are never energized at the same time (break-before-make).
// - actuatorBegin() forces both relays off and must be the first hardware
//   call in setup().
void actuatorBegin();
void actuatorUp();
void actuatorDown();
void actuatorOff();
ActState actuatorState();

void actuatorSetCap(uint32_t capMs);  // max continuous energize before forced off; 0 = disabled
void actuatorTick(uint32_t nowMs);    // enforces the cap; call every loop
bool actuatorCapTripped();            // latched true after a cap trip until next up()/down()
