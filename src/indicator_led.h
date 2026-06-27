#pragma once
#include "protocol.h"
// =====================================================================
// Agent Buddy — on-board LED feedback
// =====================================================================
//
// Gives every state a distinct, non-blocking blink "personality" so the
// state machine is observable today, before the LCD exists. Each state
// maps to a pattern: a list of millisecond steps that alternate ON/OFF.

class IndicatorLed {
 public:
  void begin();
  // Re-point the pattern when the state changes.
  void onStateChange(BuddyState s);
  // Advance the blink pattern; call every loop.
  void update(uint32_t now);

 private:
  void write(bool on);

  const uint16_t* steps_ = nullptr;
  uint8_t  stepCount_ = 0;
  uint8_t  stepIndex_ = 0;
  uint32_t lastToggle_ = 0;
  bool     ledOn_ = false;
};
