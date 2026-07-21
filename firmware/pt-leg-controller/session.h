#pragma once
#include <stdint.h>

enum class Phase { Idle, Lift, Hold, Lower, Rest, SafeLower, Fault };

struct CycleCfg {
  uint32_t maxTravelMs;  // LIFT/LOWER/SafeLower safety cap
  uint32_t holdMs;
  uint32_t restMs;
};

class SessionSM {
 public:
  CycleCfg cfg{8000, 10000, 10000};
  void start(uint32_t nowMs);        // Idle/Fault -> Lift (start also clears a latched Fault)
  void requestStop(uint32_t nowMs);  // clean stop -> SafeLower (or straight off from Rest)
  void forceFault(uint32_t nowMs);   // external safety trip (actuator cap)
  void tick(uint32_t nowMs, bool topPressed, bool bottomPressed);
  bool running() const { return ph != Phase::Idle && ph != Phase::Fault; }
  bool faulted() const { return ph == Phase::Fault; }
  Phase phase() const { return ph; }
  uint32_t elapsedSec(uint32_t nowMs) const;
  uint16_t reps() const { return repCount; }
  bool takeStartedEdge();
  bool takeStoppedEdge();  // fires after safe-lower AND on entering FAULT
  uint32_t finalElapsedSec() const { return finalElapsed; }
  uint16_t finalReps() const { return finalRepCount; }

 private:
  void enter(Phase p, uint32_t nowMs);
  void finish(uint32_t nowMs, Phase to);  // record final elapsed/reps, raise stoppedEdge, go to `to`
  Phase ph = Phase::Idle;
  uint32_t phaseStart = 0, sessionStart = 0;
  uint16_t repCount = 0;
  uint32_t finalElapsed = 0;
  uint16_t finalRepCount = 0;
  bool startedEdge = false, stoppedEdge = false;
};
