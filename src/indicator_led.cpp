#include "indicator_led.h"
#include "config.h"

// Each pattern is a sequence of durations (ms) that alternate ON, OFF,
// ON, OFF... and then loops. This makes states feel distinct on a single
// LED: busy states blink fast, done glows almost-solid, error is urgent.
namespace {
const uint16_t P_IDLE[]     = {  60, 2400 };              // a quiet heartbeat
const uint16_t P_STARTING[] = { 120, 120, 120, 120, 120, 600 };  // booting up
const uint16_t P_THINKING[] = { 450, 450 };              // slow ponder
const uint16_t P_CODING[]   = { 140, 140 };              // busy typing
const uint16_t P_TESTING[]  = { 110, 110, 110, 520 };    // double-check
const uint16_t P_DONE[]     = { 1600, 180 };             // celebratory glow
const uint16_t P_ERROR[]    = {  90,  90 };              // urgent
const uint16_t P_MESSAGE[]  = { 130, 130, 130, 760 };    // "ping" attention

struct Pat { const uint16_t* s; uint8_t n; };
Pat patternFor(BuddyState st) {
  switch (st) {
    case BuddyState::Starting: return { P_STARTING, 6 };
    case BuddyState::Thinking: return { P_THINKING, 2 };
    case BuddyState::Coding:   return { P_CODING,   2 };
    case BuddyState::Testing:  return { P_TESTING,  4 };
    case BuddyState::Done:     return { P_DONE,     2 };
    case BuddyState::Error:    return { P_ERROR,    2 };
    case BuddyState::Message:  return { P_MESSAGE,  4 };
    case BuddyState::Idle:
    default:                   return { P_IDLE,     2 };
  }
}
}  // namespace

void IndicatorLed::write(bool on) {
  ledOn_ = on;
#if BUDDY_LED_ACTIVE_LOW
  digitalWrite(BUDDY_LED_PIN, on ? LOW : HIGH);
#else
  digitalWrite(BUDDY_LED_PIN, on ? HIGH : LOW);
#endif
}

void IndicatorLed::begin() {
  pinMode(BUDDY_LED_PIN, OUTPUT);
  onStateChange(BuddyState::Idle);
}

void IndicatorLed::onStateChange(BuddyState s) {
  Pat p = patternFor(s);
  steps_ = p.s;
  stepCount_ = p.n;
  stepIndex_ = 0;
  lastToggle_ = millis();
  write(true);  // every pattern starts on an ON step
}

void IndicatorLed::update(uint32_t now) {
  if (!steps_ || stepCount_ == 0) return;
  uint16_t dwell = steps_[stepIndex_];
  if (now - lastToggle_ >= dwell) {
    stepIndex_ = (stepIndex_ + 1) % stepCount_;
    lastToggle_ = now;
    write((stepIndex_ % 2) == 0);  // even index = ON step
  }
}
