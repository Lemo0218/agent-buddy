// =====================================================================
// Agent Buddy — XIAO ESP32-S3 firmware  (entry point)
// =====================================================================
//
//   PC bridge --NDJSON line--> [USB-CDC] --> parse --> StateMachine
//        StateMachine --> IndicatorLed + Display + ServoMotion
//
// Today the LED + Serial face make the whole pipeline observable with no
// extra hardware. Wire an LCD/servo and flip the flags in config.h.

#include <Arduino.h>
#include "config.h"
#include "protocol.h"
#include "state_machine.h"
#include "indicator_led.h"
#include "display.h"
#include "servo_motion.h"
#include "pet.h"

static StateMachine  sm;
static IndicatorLed  led;
static Display        display;
static ServoMotion   servo;
static Pet           pet;

// Line accumulator for incoming NDJSON.
static char    lineBuf[512];
static size_t  lineLen = 0;

static void sendAck(const BuddyEvent& ev) {
  // Compact machine-readable ack so the PC bridge can confirm delivery.
  Serial.print("{\"ack\":\"");
  Serial.print(stateName(ev.state));
  Serial.print("\",\"mood\":\"");
  Serial.print(moodName(ev.mood));
  Serial.print("\",\"motion\":\"");
  Serial.print(motionName(ev.motion));
  Serial.print("\",\"ttl_ms\":");
  Serial.print(ev.ttl_ms);
  Serial.println("}");
}

static void onLine(const char* line) {
  if (!*line) return;

  // 1) Pet care command? ({"cmd":"feed"} ...) -> apply + reply stats.
  char cmd[24];
  if (parseCommand(line, cmd, sizeof cmd)) {
    if (!strcmp(cmd, "usage")) {            // daily + 5h usage push from the PC
      float c; uint32_t tk; int p, bp;
      if (parseUsage(line, c, tk, p, bp)) display.setUsage(c, tk, p, bp);
      Serial.println("{\"ok\":\"usage\"}");
      return;
    }
    if (!strcmp(cmd, "world")) {            // multi-session roster from the PC
      WorldRoom rooms[6]; int rp = -1, rm = -1;
      int n = parseWorld(line, rooms, 6, rp, rm);
      if (n >= 0) display.setWorld(rooms, n, rp, rm);
      Serial.println("{\"ok\":\"world\"}");
      return;
    }
    BuddyEvent reaction;
    bool hasReaction = pet.applyCommand(cmd, reaction);
    char buf[220];
    pet.toJson(buf, sizeof buf);
    Serial.println(buf);                 // reply current stats to the PC panel
    if (hasReaction) sm.apply(reaction, millis());
    return;
  }

  // 2) Otherwise a status event ({"event":"coding"} ...).
  BuddyEvent ev;
  if (!parseEvent(line, ev)) {
    Serial.print("[warn] bad line: ");
    Serial.println(line);
    return;
  }
  if (ev.state == BuddyState::Done) pet.gainFromWork();  // coding feeds the pet
  sm.apply(ev, millis());
}

static void renderState() {
  const BuddyEvent& ev = sm.state();
  led.onStateChange(ev.state);
  display.render(ev);
  servo.play(ev.motion, millis());
  sendAck(ev);
}

void setup() {
  Serial.begin(BUDDY_SERIAL_BAUD);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0 < 1500)) { /* wait briefly for USB host */ }

  led.begin();
  display.begin();
  servo.begin();
  sm.begin();
  pet.begin();

  Serial.println();
  Serial.print("Agent Buddy v");
  Serial.print(BUDDY_FW_VERSION);
  Serial.println(" — XIAO ESP32-S3");
  Serial.println("Send one NDJSON event per line, e.g.:");
  Serial.println("  {\"v\":1,\"event\":\"coding\",\"text\":\"editing\",\"mood\":\"focus\",\"motion\":\"nod\",\"ttl_ms\":8000}");

  // Greeting animation on boot (brief: "장치가 인사 애니메이션").
  BuddyEvent hello;
  hello.state = BuddyState::Starting;
  hello.text = "Hi! I'm your buddy";
  hello.mood = BuddyMood::Happy;
  hello.motion = BuddyMotion::Nod;
  hello.ttl_ms = 2500;     // greet, then relax to idle
  sm.apply(hello, millis());
}

void loop() {
  // 1) Drain incoming serial into complete lines.
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (lineLen > 0) {
        lineBuf[lineLen] = '\0';
        onLine(lineBuf);
        lineLen = 0;
      }
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    } else {
      lineLen = 0;  // overflow -> drop the runaway line
    }
  }

  uint32_t now = millis();

  // 2) Advance pet simulation + state machine.
  pet.tick(now);
  sm.update(now);

  // 3) On entry into a new state, fire all renderers once.
  if (sm.consumeChanged()) {
    renderState();
  }

  // 4) Continuous, non-blocking outputs.
  display.setPet(pet.stats(), pet.idleMood());
  led.update(now);
  servo.update(now);
  display.tick(now);
}
