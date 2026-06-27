#pragma once
#include <Arduino.h>
// =====================================================================
// Agent Buddy — event protocol (NDJSON over USB-CDC serial)
// =====================================================================
//
// The PC bridge sends ONE JSON object per line, e.g.:
//   {"v":1,"event":"coding","text":"Editing files","mood":"focus","motion":"nod","ttl_ms":10000}
//   {"v":1,"event":"done","text":"Done!","mood":"happy","motion":"bounce","ttl_ms":5000}
//
// `parseEvent()` turns one such line into a BuddyEvent. Unknown strings
// degrade gracefully to sane defaults rather than failing the whole line.

enum class BuddyState : uint8_t {
  Idle, Starting, Thinking, Coding, Testing, Done, Error, Message, React, Unknown
};

enum class BuddyMood : uint8_t {
  Neutral, Focus, Happy, Sad, Surprised, Sleepy, Confused
};

enum class BuddyMotion : uint8_t {
  None, Nod, Shake, Bounce, Tilt
};

struct BuddyEvent {
  int         version = 1;
  BuddyState  state   = BuddyState::Idle;
  String      text    = "";
  BuddyMood   mood    = BuddyMood::Neutral;
  BuddyMotion motion  = BuddyMotion::None;
  uint32_t    ttl_ms  = 0;   // 0 = stays until next event
};

// String <-> enum helpers (also used for logging back to the PC).
BuddyState  parseState(const char* s);
BuddyMood   parseMood(const char* s);
BuddyMotion parseMotion(const char* s);
const char* stateName(BuddyState s);
const char* moodName(BuddyMood m);
const char* motionName(BuddyMotion m);

// Parse one NDJSON line. Returns true if it produced a usable event.
bool parseEvent(const char* line, BuddyEvent& out);

// If the line is a command ({"cmd":"feed"}), copy the verb into `out` and
// return true. Otherwise return false (caller should try parseEvent).
bool parseCommand(const char* line, char* out, size_t n);

// Parse a usage update line ({"cmd":"usage","cost":,"tok":,"pct":,"bpct":}).
// bpct = 5-hour block usage vs block budget (-1 if absent).
bool parseUsage(const char* line, float& cost, uint32_t& tok, int& pct, int& bpct);

// Multi-session "world" roster. st: 0 idle, 1 working, 2 waiting, 3 done.
// fill: 0-100 water level (session usage vs daily budget). cost: session $.
struct WorldRoom { uint8_t st; uint8_t fill; float cost; char label[12]; };
// Parse {"cmd":"world","reset":<0-100>,"rmin":<min>,"rooms":[{"st","f","c","p"}]}.
// Fills the rooms array and the global reset gauge. Returns room count
// (0..maxn), or -1 if the line is not a world command.
int parseWorld(const char* line, WorldRoom* out, int maxn, int& resetPct, int& resetMin);
