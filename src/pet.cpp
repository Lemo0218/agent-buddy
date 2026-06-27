#include "pet.h"
#include <Preferences.h>
#include <math.h>

namespace { Preferences nvs; }

// ---- decay rates (per second, "hardcore" but USB-always-on friendly) ----
static const float R_HUNGER = 100.0f / (3.5f * 3600);
static const float R_HAPPY  = 100.0f / (5.0f * 3600);
static const float R_ENERGY = 100.0f / (7.0f * 3600);
static const float R_SLEEP  = 100.0f / (2.0f * 3600);   // energy regained while sleeping
static const uint32_t FAINT_AFTER_MS = 2UL * 3600 * 1000;  // 2h sick -> faint

static uint8_t clamp8(float v) { return (uint8_t)(v < 0 ? 0 : (v > 100 ? 100 : v)); }

void Pet::begin() {
  load();
  uint32_t now = millis();
  bornMs_ = now; lastTick_ = now; lastSave_ = now;
}

void Pet::load() {
  nvs.begin("pet", false);
  bool exists = nvs.isKey("xp");
  hF_  = nvs.getFloat("hunger", 100);
  haF_ = nvs.getFloat("happy",  100);
  eF_  = nvs.getFloat("energy", 100);
  heF_ = nvs.getFloat("health", 100);
  s_.xp     = nvs.getUInt("xp", 0);
  baseAge_  = nvs.getUInt("age", 0);
  s_.ageMin = baseAge_;
  s_.life   = (PetLife)nvs.getUChar("life", (uint8_t)PetLife::Ok);
  if (!exists) { hF_=haF_=eF_=heF_=100; s_.xp=0; s_.ageMin=0; s_.life=PetLife::Ok; }
  if (s_.life == PetLife::Sleeping) s_.life = PetLife::Ok;  // wake on boot
  recompute();
}

void Pet::save() {
  nvs.putFloat("hunger", hF_); nvs.putFloat("happy", haF_);
  nvs.putFloat("energy", eF_); nvs.putFloat("health", heF_);
  nvs.putUInt("xp", s_.xp); nvs.putUInt("age", s_.ageMin);
  nvs.putUChar("life", (uint8_t)s_.life);
}

void Pet::recompute() {
  s_.hunger = clamp8(hF_); s_.happy = clamp8(haF_);
  s_.energy = clamp8(eF_); s_.health = clamp8(heF_);
  s_.level  = 1 + s_.xp / 100; if (s_.level > 99) s_.level = 99;
}

uint8_t Pet::evoTier() const {
  if (s_.level <= 2) return 0;
  if (s_.level <= 5) return 1;
  if (s_.level <= 10) return 2;
  return 3;
}

void Pet::tick(uint32_t now) {
  float dt = (now - lastTick_) / 1000.0f;
  if (dt < 1.0f) return;             // process ~1 Hz
  lastTick_ = now;
  s_.ageMin = baseAge_ + (now - bornMs_) / 60000;

  if (s_.life == PetLife::Faint) { return; }   // game over until hatched

  bool sleeping = (s_.life == PetLife::Sleeping);
  hF_  -= R_HUNGER * dt * (sleeping ? 0.4f : 1.0f);
  haF_ -= R_HAPPY  * dt * (sleeping ? 0.4f : 1.0f);
  eF_  += (sleeping ? R_SLEEP : -R_ENERGY) * dt;

  // health responds to care
  if (hF_ < 15 || haF_ < 15)            heF_ -= 0.03f * dt;
  else if (hF_ > 50 && haF_ > 50 && eF_ > 30) heF_ += 0.02f * dt;

  hF_  = hF_  < 0 ? 0 : hF_;  haF_ = haF_ < 0 ? 0 : haF_;
  eF_  = eF_  < 0 ? 0 : (eF_ > 100 ? 100 : eF_);
  heF_ = heF_ < 0 ? 0 : (heF_ > 100 ? 100 : heF_);

  if (sleeping && eF_ >= 100) s_.life = PetLife::Ok;     // auto-wake when rested

  if (heF_ <= 0 && s_.life == PetLife::Ok) s_.life = PetLife::Sick;
  if (s_.life == PetLife::Sick) {
    sickMs_ += (uint32_t)(dt * 1000);
    if (sickMs_ > FAINT_AFTER_MS) s_.life = PetLife::Faint;
  } else sickMs_ = 0;

  recompute();
  if (now - lastSave_ > 30000) { save(); lastSave_ = now; }
}

static void react(BuddyEvent& e, BuddyState st, BuddyMood m, const char* txt,
                  BuddyMotion mo, uint32_t ttl) {
  e.state = st; e.mood = m; e.text = txt; e.motion = mo; e.ttl_ms = ttl;
}

bool Pet::applyCommand(const char* cmd, BuddyEvent& r) {
  bool hasReaction = true;
  if (!strcmp(cmd, "feed")) {
    hF_ = min(100.0f, hF_ + 35); haF_ = min(100.0f, haF_ + 4); s_.xp += 2;
    react(r, BuddyState::React, BuddyMood::Happy, "Yum!", BuddyMotion::Bounce, 1600);
  } else if (!strcmp(cmd, "play")) {
    haF_ = min(100.0f, haF_ + 30); eF_ = max(0.0f, eF_ - 15); hF_ = max(0.0f, hF_ - 5); s_.xp += 5;
    react(r, BuddyState::React, BuddyMood::Happy, "Wheee!", BuddyMotion::Bounce, 1600);
  } else if (!strcmp(cmd, "pet")) {
    haF_ = min(100.0f, haF_ + 12); s_.xp += 1;
    react(r, BuddyState::React, BuddyMood::Happy, "<3", BuddyMotion::Nod, 1400);
  } else if (!strcmp(cmd, "sleep")) {
    s_.life = PetLife::Sleeping;
    react(r, BuddyState::React, BuddyMood::Sleepy, "zzz...", BuddyMotion::None, 1500);
  } else if (!strcmp(cmd, "wake")) {
    if (s_.life == PetLife::Sleeping) s_.life = PetLife::Ok;
    react(r, BuddyState::React, BuddyMood::Surprised, "morning!", BuddyMotion::Tilt, 1400);
  } else if (!strcmp(cmd, "medicine")) {
    heF_ = max(heF_, 60.0f); if (s_.life == PetLife::Sick) s_.life = PetLife::Ok; sickMs_ = 0;
    react(r, BuddyState::React, BuddyMood::Happy, "better!", BuddyMotion::Nod, 1500);
  } else if (!strcmp(cmd, "hatch")) {
    hF_=haF_=eF_=heF_=100; s_.xp=0; s_.ageMin=0; baseAge_=0; s_.life=PetLife::Ok; sickMs_=0;
    nvs.putUInt("age", 0); bornMs_ = millis(); save();
    react(r, BuddyState::React, BuddyMood::Happy, "hi! :)", BuddyMotion::Bounce, 2000);
  } else {
    hasReaction = false;   // "stats" or unknown -> no reaction, just reply
  }
  recompute();
  save(); lastSave_ = millis();
  return hasReaction;
}

void Pet::gainFromWork() {
  s_.xp += 15;
  hF_ = min(100.0f, hF_ + 8); haF_ = min(100.0f, haF_ + 5);
  recompute();
}

BuddyMood Pet::idleMood() const {
  if (s_.life == PetLife::Faint) return BuddyMood::Sad;
  if (s_.life == PetLife::Sick)  return BuddyMood::Sad;
  if (s_.life == PetLife::Sleeping) return BuddyMood::Sleepy;
  if (s_.energy < 20) return BuddyMood::Sleepy;
  if (s_.hunger < 25 || s_.happy < 25) return BuddyMood::Sad;
  if (s_.happy > 70 && s_.hunger > 50) return BuddyMood::Happy;
  return BuddyMood::Neutral;
}

void Pet::toJson(char* buf, size_t n) const {
  const char* lf = s_.life == PetLife::Faint ? "faint" :
                   s_.life == PetLife::Sick ? "sick" :
                   s_.life == PetLife::Sleeping ? "sleep" : "ok";
  snprintf(buf, n,
    "{\"pet\":{\"life\":\"%s\",\"level\":%u,\"xp\":%lu,\"tier\":%u,"
    "\"hunger\":%u,\"happy\":%u,\"energy\":%u,\"health\":%u,\"age_min\":%lu}}",
    lf, s_.level, (unsigned long)s_.xp, evoTier(),
    s_.hunger, s_.happy, s_.energy, s_.health, (unsigned long)s_.ageMin);
}
