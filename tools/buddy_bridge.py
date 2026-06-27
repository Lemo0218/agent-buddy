#!/usr/bin/env python3
"""Agent Buddy — PC bridge for Claude Code (and any agent) hooks.

Called by Claude Code hooks to push a status event to the desk pet over USB.
Maps a short preset name to a full NDJSON event and writes one line to the
board's serial port. Designed to be FAST and to FAIL SILENTLY (always exits 0)
so it can never block or break the coding session if the board is unplugged.

Usage (from a hook):
    /usr/bin/python3.10 buddy_bridge.py done
    /usr/bin/python3.10 buddy_bridge.py waiting
    /usr/bin/python3.10 buddy_bridge.py <preset> [--port /dev/ttyACM0]

Presets: done | waiting | thinking | working | start | idle | error
"""
import sys
import glob
import json

PRESETS = {
    # Claude finished its reply -> celebrate, "it's ready"
    "done":     {"event": "done",     "text": "Done!",       "mood": "happy",     "motion": "bounce"},
    # Claude needs you (permission / input) -> get attention
    "waiting":  {"event": "message",  "text": "Your turn",   "mood": "surprised", "motion": "tilt"},
    "thinking": {"event": "thinking", "text": "Thinking...", "mood": "focus",     "motion": "tilt"},
    "working":  {"event": "coding",   "text": "Working...",  "mood": "focus",     "motion": "nod"},
    "start":    {"event": "starting", "text": "On it!",      "mood": "happy",     "motion": "nod"},
    "error":    {"event": "error",    "text": "Error",       "mood": "sad",       "motion": "shake"},
    "idle":     {"event": "idle",     "text": "",            "mood": "neutral",   "motion": "none"},
}


def find_port(explicit=None):
    if explicit:
        return explicit
    cands = sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*"))
    return cands[0] if cands else None


def main():
    args = sys.argv[1:]
    preset = args[0] if args else "done"
    port = None
    if "--port" in args:
        port = args[args.index("--port") + 1]

    ev = dict(PRESETS.get(preset, PRESETS["done"]))
    ev = {"v": 1, **ev, "ttl_ms": 0}   # ttl 0 = hold until next event

    port = find_port(port)
    if not port:
        return  # no board connected -> silently do nothing

    try:
        import serial  # pyserial (available under /usr/bin/python3.10 here)
        line = ("\n" + json.dumps(ev, ensure_ascii=False) + "\n").encode("utf-8")
        s = serial.Serial(port, 115200, timeout=0.3, write_timeout=0.5)
        s.write(line)
        s.flush()
        s.close()
    except Exception:
        pass  # never break the agent session


if __name__ == "__main__":
    try:
        main()
    finally:
        sys.exit(0)  # hooks must not see a failure
