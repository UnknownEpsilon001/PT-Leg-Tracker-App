#include "session.h"

void SessionSM::enter(Phase p, uint32_t nowMs) {
  ph = p;
  phaseStart = nowMs;
}

void SessionSM::finish(uint32_t nowMs, Phase to) {
  finalElapsed = (ph == Phase::Idle) ? 0 : (nowMs - sessionStart) / 1000;
  finalRepCount = repCount;
  stoppedEdge = true;
  enter(to, nowMs);
}

void SessionSM::start(uint32_t nowMs) {
  if (running()) return;  // ignore if already running; allowed from Idle or Fault
  sessionStart = nowMs;
  repCount = 0;
  startedEdge = true;
  enter(Phase::Lift, nowMs);
}

void SessionSM::requestStop(uint32_t nowMs) {
  switch (ph) {
    case Phase::Idle:
    case Phase::Fault:
      return;  // nothing to stop; a latched Fault clears only via start()
    case Phase::Lift:
    case Phase::Hold:
    case Phase::Lower:
      enter(Phase::SafeLower, nowMs);  // bring the leg down, then stop
      return;
    case Phase::Rest:
      finish(nowMs, Phase::Idle);  // already down
      return;
    case Phase::SafeLower:
      return;  // already lowering to a clean stop
  }
}

void SessionSM::forceFault(uint32_t nowMs) {
  if (ph == Phase::Idle || ph == Phase::Fault) return;
  finish(nowMs, Phase::Fault);
}

void SessionSM::tick(uint32_t nowMs, bool topPressed, bool bottomPressed) {
  // net 2: both switches pressed at once is physically impossible -> wiring fault
  if (running() && topPressed && bottomPressed) {
    finish(nowMs, Phase::Fault);
    return;
  }
  uint32_t dt = nowMs - phaseStart;
  switch (ph) {
    case Phase::Idle:
    case Phase::Fault:
      return;
    case Phase::Lift:
      if (topPressed) enter(Phase::Hold, nowMs);            // fully up
      else if (dt >= cfg.maxTravelMs) finish(nowMs, Phase::Fault);  // net 1
      return;
    case Phase::Hold:
      if (dt >= cfg.holdMs) enter(Phase::Lower, nowMs);
      return;
    case Phase::Lower:
      if (bottomPressed) enter(Phase::Rest, nowMs);         // fully down
      else if (dt >= cfg.maxTravelMs) finish(nowMs, Phase::Fault);  // net 1
      return;
    case Phase::Rest:
      if (dt >= cfg.restMs) {
        repCount++;
        enter(Phase::Lift, nowMs);
      }
      return;
    case Phase::SafeLower:
      // clean stop: lower until bottom switch OR the cap, then Idle (NOT Fault)
      if (bottomPressed || dt >= cfg.maxTravelMs) finish(nowMs, Phase::Idle);
      return;
  }
}

uint32_t SessionSM::elapsedSec(uint32_t nowMs) const {
  if (ph == Phase::Idle) return 0;
  if (ph == Phase::Fault) return finalElapsed;  // frozen at fault time
  return (nowMs - sessionStart) / 1000;
}

bool SessionSM::takeStartedEdge() {
  bool v = startedEdge;
  startedEdge = false;
  return v;
}

bool SessionSM::takeStoppedEdge() {
  bool v = stoppedEdge;
  stoppedEdge = false;
  return v;
}
