#pragma once
#include "protocol.h"
#include "pet.h"
// =====================================================================
// Agent Buddy — face/speech-bubble renderer (LCD)
// =====================================================================
//
// Abstraction over the buddy's "face". With no LCD wired (BUDDY_HAS_DISPLAY
// == 0) it prints a compact ASCII face + speech bubble to Serial, so you can
// watch expressions change today. When the SPI TFT is connected, fill in the
// drawing code in display.cpp (marked TODO) and set BUDDY_HAS_DISPLAY = 1.

class Display {
 public:
  void begin();
  void render(const BuddyEvent& ev);   // call on state change
  void tick(uint32_t now);             // frame-based animation
  void setPet(const PetStats& st, BuddyMood idleMood);  // feed pet state in
  void setUsage(float cost, uint32_t tokens, int pct, int blockPct);  // daily + 5h usage
  void setWorld(const WorldRoom* rooms, int n, int resetPct, int resetMin);

 private:
  bool       ready_ = false;
  BuddyEvent cur_;              // last rendered event (for tick animation)
  uint32_t   lastAnim_ = 0;
  uint8_t    dotPhase_ = 0;
  PetStats   pet_{};
  BuddyMood  petMood_ = BuddyMood::Neutral;
};
