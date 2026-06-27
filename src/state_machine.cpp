#include "state_machine.h"

void StateMachine::begin() {
  current_ = BuddyEvent{};            // defaults to Idle
  enteredAt_ = 0;
  changed_ = true;                    // render the initial idle state once
}

void StateMachine::goIdle(uint32_t now) {
  BuddyEvent idle;                    // fresh defaults = Idle / Neutral / None
  current_ = idle;
  enteredAt_ = now;
  changed_ = true;
}

void StateMachine::apply(const BuddyEvent& ev, uint32_t now) {
  current_ = ev;
  enteredAt_ = now;
  changed_ = true;
}

void StateMachine::update(uint32_t now) {
  // A non-zero TTL means "hold this state for ttl_ms, then relax to idle".
  if (current_.state != BuddyState::Idle && current_.ttl_ms > 0) {
    if (now - enteredAt_ >= current_.ttl_ms) {
      goIdle(now);
    }
  }
}

bool StateMachine::consumeChanged() {
  if (!changed_) return false;
  changed_ = false;
  return true;
}
