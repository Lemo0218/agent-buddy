#pragma once
#include <Arduino.h>
#include "protocol.h"
// =====================================================================
// Agent Buddy — Tamagotchi-style pet simulation (lives on the board)
// =====================================================================
//
// Stats decay over (powered) time and are persisted to NVS so the pet
// survives reboots. Actions arrive as commands from the PC control panel
// ({"cmd":"feed"} ...). Coding activity (Claude 'done') also feeds + levels
// the pet. Neglect -> sick -> faint (recoverable by hatching a new egg).

enum class PetLife : uint8_t { Ok, Sick, Sleeping, Faint };

struct PetStats {
  uint8_t  hunger;    // 100 = full, 0 = starving
  uint8_t  happy;     // 100 = joyful
  uint8_t  energy;    // 100 = rested
  uint8_t  health;    // 100 = healthy
  uint32_t xp;
  uint8_t  level;
  uint32_t ageMin;
  PetLife  life;
};

class Pet {
 public:
  void begin();
  void tick(uint32_t now);                 // decay + autosave

  // Returns a short reaction event for the display ("" if none), and fills
  // a JSON reply (the caller writes it back to the PC).
  bool applyCommand(const char* cmd, BuddyEvent& reactionOut);

  void gainFromWork();                     // Claude 'done' -> xp + food

  PetStats stats() const { return s_; }
  PetLife  life() const  { return s_.life; }
  BuddyMood idleMood() const;              // mood shown when resting
  uint8_t  evoTier() const;                // 0 baby .. 3 adult (by level)
  void toJson(char* buf, size_t n) const;

 private:
  void   load();
  void   save();
  void   recompute();                      // level from xp, life from health

  PetStats s_{};
  // internal floats for smooth decay
  float hF_=100, haF_=100, eF_=100, heF_=100;
  uint32_t bornMs_=0, lastTick_=0, lastSave_=0, sickMs_=0, baseAge_=0;
};
