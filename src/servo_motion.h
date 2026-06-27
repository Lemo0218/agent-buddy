#pragma once
#include "protocol.h"
// =====================================================================
// Agent Buddy — head motion (1 servo)
// =====================================================================
//
// Plays a short, non-blocking head gesture per motion kind (nod / shake /
// bounce / tilt). With no servo wired (BUDDY_HAS_SERVO == 0) it logs the
// intended gesture to Serial so the protocol path is still observable.

class ServoMotion {
 public:
  void begin();
  void play(BuddyMotion m, uint32_t now);  // start a gesture
  void update(uint32_t now);               // advance it (non-blocking)

 private:
  void writeAngle(int deg);

  BuddyMotion active_ = BuddyMotion::None;
  uint8_t  phase_ = 0;
  uint32_t phaseStart_ = 0;
  int      centerDeg_ = 90;
};
