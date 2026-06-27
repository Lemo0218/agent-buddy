#include "protocol.h"
#include <ArduinoJson.h>

namespace {
// Tiny case-insensitive equality for short protocol tokens.
bool ieq(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    ++a; ++b;
  }
  return *a == *b;
}
}  // namespace

BuddyState parseState(const char* s) {
  if (ieq(s, "idle"))     return BuddyState::Idle;
  if (ieq(s, "starting")) return BuddyState::Starting;
  if (ieq(s, "thinking")) return BuddyState::Thinking;
  if (ieq(s, "coding"))   return BuddyState::Coding;
  if (ieq(s, "testing"))  return BuddyState::Testing;
  if (ieq(s, "done"))     return BuddyState::Done;
  if (ieq(s, "error"))    return BuddyState::Error;
  if (ieq(s, "message"))  return BuddyState::Message;
  if (ieq(s, "react"))    return BuddyState::React;
  return BuddyState::Unknown;
}

BuddyMood parseMood(const char* s) {
  if (ieq(s, "focus"))     return BuddyMood::Focus;
  if (ieq(s, "happy"))     return BuddyMood::Happy;
  if (ieq(s, "sad"))       return BuddyMood::Sad;
  if (ieq(s, "surprised")) return BuddyMood::Surprised;
  if (ieq(s, "sleepy"))    return BuddyMood::Sleepy;
  if (ieq(s, "confused"))  return BuddyMood::Confused;
  return BuddyMood::Neutral;
}

BuddyMotion parseMotion(const char* s) {
  if (ieq(s, "nod"))    return BuddyMotion::Nod;
  if (ieq(s, "shake"))  return BuddyMotion::Shake;
  if (ieq(s, "bounce")) return BuddyMotion::Bounce;
  if (ieq(s, "tilt"))   return BuddyMotion::Tilt;
  return BuddyMotion::None;
}

const char* stateName(BuddyState s) {
  switch (s) {
    case BuddyState::Idle:     return "idle";
    case BuddyState::Starting: return "starting";
    case BuddyState::Thinking: return "thinking";
    case BuddyState::Coding:   return "coding";
    case BuddyState::Testing:  return "testing";
    case BuddyState::Done:     return "done";
    case BuddyState::Error:    return "error";
    case BuddyState::Message:  return "message";
    case BuddyState::React:    return "react";
    default:                   return "unknown";
  }
}

const char* moodName(BuddyMood m) {
  switch (m) {
    case BuddyMood::Focus:     return "focus";
    case BuddyMood::Happy:     return "happy";
    case BuddyMood::Sad:       return "sad";
    case BuddyMood::Surprised: return "surprised";
    case BuddyMood::Sleepy:    return "sleepy";
    case BuddyMood::Confused:  return "confused";
    default:                   return "neutral";
  }
}

const char* motionName(BuddyMotion m) {
  switch (m) {
    case BuddyMotion::Nod:    return "nod";
    case BuddyMotion::Shake:  return "shake";
    case BuddyMotion::Bounce: return "bounce";
    case BuddyMotion::Tilt:   return "tilt";
    default:                  return "none";
  }
}

bool parseEvent(const char* line, BuddyEvent& out) {
  if (!line || !*line) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) return false;

  // `event` is the only required field.
  const char* evt = doc["event"] | "";
  if (!*evt) return false;

  out.version = doc["v"] | 1;
  out.state   = parseState(evt);
  out.text    = (const char*)(doc["text"]   | "");
  out.mood    = parseMood(doc["mood"]   | "");
  out.motion  = parseMotion(doc["motion"] | "");
  out.ttl_ms  = doc["ttl_ms"] | 0u;
  return true;
}

bool parseCommand(const char* line, char* out, size_t n) {
  if (!line || !*line) return false;
  JsonDocument doc;
  if (deserializeJson(doc, line)) return false;
  const char* cmd = doc["cmd"] | "";
  if (!*cmd) return false;
  strncpy(out, cmd, n - 1);
  out[n - 1] = '\0';
  return true;
}

bool parseUsage(const char* line, float& cost, uint32_t& tok, int& pct, int& bpct) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) return false;
  const char* cmd = doc["cmd"] | "";
  if (strcmp(cmd, "usage")) return false;
  cost = doc["cost"] | 0.0f;
  tok  = doc["tok"]  | 0u;
  pct  = doc["pct"]  | 0;
  bpct = doc["bpct"] | -1;
  return true;
}

int parseWorld(const char* line, WorldRoom* out, int maxn, int& resetPct, int& resetMin) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) return -1;
  if (strcmp(doc["cmd"] | "", "world")) return -1;
  resetPct = doc["reset"] | -1;
  resetMin = doc["rmin"]  | -1;
  int n = 0;
  for (JsonObject r : doc["rooms"].as<JsonArray>()) {
    if (n >= maxn) break;
    const char* s = r["st"] | "idle";
    uint8_t st = (!strcmp(s,"working")||!strcmp(s,"thinking")||!strcmp(s,"coding")||
                  !strcmp(s,"testing")||!strcmp(s,"starting")) ? 1 :
                 (!strcmp(s,"waiting")||!strcmp(s,"message"))   ? 2 :
                 (!strcmp(s,"done"))                            ? 3 : 0;
    out[n].st   = st;
    out[n].fill = r["f"] | 0;
    out[n].cost = r["c"] | 0.0f;
    strncpy(out[n].label, r["p"] | "", sizeof out[n].label - 1);
    out[n].label[sizeof out[n].label - 1] = '\0';
    ++n;
  }
  return n;
}
