#include "servo_motion.h"
#include "config.h"

#if BUDDY_HAS_SERVO
#include <ESP32Servo.h>
namespace { Servo s_servo; }
#endif

// A gesture is a list of (angle-offset, hold-ms) keyframes around center.
// Kept short so it never blocks the main loop for long.
namespace {
struct Key { int8_t offset; uint16_t holdMs; };

const Key K_NOD[]    = { {  0, 80 }, { +25, 160 }, { -10, 160 }, { 0, 120 } };
const Key K_SHAKE[]  = { { -25, 120 }, { +25, 120 }, { -20, 120 }, { +20, 120 }, { 0, 100 } };
const Key K_BOUNCE[] = { { +20, 110 }, { -15, 110 }, { +12, 110 }, { 0, 120 } };
const Key K_TILT[]   = { { +22, 280 }, { 0, 200 } };

struct Seq { const Key* k; uint8_t n; };
Seq seqFor(BuddyMotion m) {
  switch (m) {
    case BuddyMotion::Nod:    return { K_NOD,    4 };
    case BuddyMotion::Shake:  return { K_SHAKE,  5 };
    case BuddyMotion::Bounce: return { K_BOUNCE, 4 };
    case BuddyMotion::Tilt:   return { K_TILT,   2 };
    default:                  return { nullptr,  0 };
  }
}
Seq s_seq;
}  // namespace

void ServoMotion::writeAngle(int deg) {
  if (deg < 0)   deg = 0;
  if (deg > 180) deg = 180;
#if BUDDY_HAS_SERVO
  s_servo.write(deg);
#else
  (void)deg;
#endif
}

void ServoMotion::begin() {
#if BUDDY_HAS_SERVO
  s_servo.setPeriodHertz(50);
  s_servo.attach(BUDDY_SERVO_PIN, BUDDY_SERVO_MIN_US, BUDDY_SERVO_MAX_US);
#endif
  writeAngle(centerDeg_);
}

void ServoMotion::play(BuddyMotion m, uint32_t now) {
  if (m == BuddyMotion::None) return;
  active_ = m;
  s_seq = seqFor(m);
  phase_ = 0;
  phaseStart_ = now;
#if !BUDDY_HAS_SERVO
  Serial.print("[servo] (virtual) gesture: ");
  Serial.println(motionName(m));
#endif
  if (s_seq.n) writeAngle(centerDeg_ + s_seq.k[0].offset);
}

void ServoMotion::update(uint32_t now) {
  if (active_ == BuddyMotion::None || s_seq.n == 0) return;
  const Key& cur = s_seq.k[phase_];
  if (now - phaseStart_ >= cur.holdMs) {
    phase_++;
    phaseStart_ = now;
    if (phase_ >= s_seq.n) {           // gesture finished -> recenter
      active_ = BuddyMotion::None;
      writeAngle(centerDeg_);
      return;
    }
    writeAngle(centerDeg_ + s_seq.k[phase_].offset);
  }
}
