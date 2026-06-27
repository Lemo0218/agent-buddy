#pragma once
#include "protocol.h"
// =====================================================================
// Agent Buddy — state machine
// =====================================================================
//
// Holds the buddy's current event and decides when to fall back to idle.
// Renderers (LED / display / servo) read state() each loop; they never
// own timing themselves, so behaviour stays consistent across outputs.

class StateMachine {
 public:
  void begin();

  // Apply a freshly parsed event (resets the TTL timer).
  void apply(const BuddyEvent& ev, uint32_t now);

  // Call every loop; expires TTL-bound states back to idle.
  void update(uint32_t now);

  const BuddyEvent& state() const { return current_; }

  // Returns true exactly once after the state changes (entry trigger).
  bool consumeChanged();

 private:
  void goIdle(uint32_t now);

  BuddyEvent current_;
  uint32_t   enteredAt_ = 0;
  bool       changed_   = false;
};
