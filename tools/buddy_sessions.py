#!/usr/bin/env python3
"""Agent Buddy — session-aware bridge for Claude Code hooks.

Tracks each Claude Code session (keyed by session_id from the hook payload on
stdin) in a shared file and pushes:
  1. {"cmd":"world","rooms":[{"st","f","c","p"},...]}  -> multi-session room view
  2. {"v":1,"event":..}                                 -> single-pet animation

Per-room water level "f" (usage vs daily budget) and cost "c" are filled in by
the buddy_usage.py daemon; the hooks here update STATE instantly and carry the
last-known f/c forward. The board auto-switches: 2+ sessions -> room grid;
otherwise the single tamagotchi pet. Fails silently (exit 0).

Hook usage (state = thinking | waiting | done | end):
    /usr/bin/python3.10 buddy_sessions.py thinking
"""
import sys, os, glob, json, time, fcntl, argparse

STATE_FILE = os.path.expanduser("~/.claude/buddy_sessions.json")
STALE_SEC  = 20 * 60

EVENT = {   # hook state -> (single-pet event, room state)
    "thinking": ({"event":"thinking","text":"Thinking...","mood":"focus",    "motion":"tilt"},  "working"),
    "waiting":  ({"event":"message", "text":"Your turn",  "mood":"surprised","motion":"tilt"},  "waiting"),
    "done":     ({"event":"done",    "text":"Done!",      "mood":"happy",    "motion":"bounce"},"idle"),
}

def find_port():
    c = sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*"))
    return c[0] if c else None

def read_payload():
    try:
        if not sys.stdin.isatty():
            return json.loads(sys.stdin.read() or "{}")
    except Exception:
        pass
    return {}

def update(sid, proj, transcript, room_state, remove):
    os.makedirs(os.path.dirname(STATE_FILE), exist_ok=True)
    f = open(STATE_FILE, "a+"); fcntl.flock(f, fcntl.LOCK_EX)
    try:
        f.seek(0)
        try: data = json.loads(f.read() or "{}")
        except Exception: data = {}
        now = time.time()
        data = {k: v for k, v in data.items() if now - v.get("ts", 0) < STALE_SEC}
        if remove:
            data.pop(sid, None)
        else:
            prev = data.get(sid, {})
            data[sid] = {"st": room_state, "proj": proj, "transcript": transcript or prev.get("transcript",""),
                         "ts": now, "fill": prev.get("fill", 0), "cost": prev.get("cost", 0.0),
                         "tok": prev.get("tok", 0)}
        f.seek(0); f.truncate(); f.write(json.dumps(data)); f.flush()
        rooms = sorted(data.values(), key=lambda v: v["ts"], reverse=True)[:6]
        return [{"st": r["st"], "p": (r.get("proj") or "")[:10], "f": int(r.get("fill", 0)),
                 "c": round(r.get("cost", 0.0), 2), "k": int(r.get("tok", 0))} for r in rooms]
    finally:
        fcntl.flock(f, fcntl.LOCK_UN); f.close()

def push(port, obj):
    try:
        import serial
        for _ in range(4):
            try:
                s = serial.Serial(port, 115200, timeout=0.4, write_timeout=0.5)
                s.write(("\n" + json.dumps(obj) + "\n").encode()); s.flush(); s.close(); return
            except Exception: time.sleep(0.07)
    except Exception: pass

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("state"); ap.add_argument("--sid"); ap.add_argument("--proj")
    a = ap.parse_args()
    p = read_payload()
    sid = (a.sid or p.get("session_id") or "sess")[:8]
    cwd = p.get("cwd") or os.getcwd()
    proj = a.proj or os.path.basename(cwd.rstrip("/")) or "claude"
    transcript = p.get("transcript_path", "")

    remove = (a.state == "end")
    ev_room = EVENT.get(a.state)
    room_state = "idle" if remove else (ev_room[1] if ev_room else "idle")
    rooms = update(sid, proj, transcript, room_state, remove)

    port = find_port()
    if not port: return
    push(port, {"cmd": "world", "rooms": rooms})
    if ev_room:
        push(port, {"v": 1, **ev_room[0], "ttl_ms": 0})

if __name__ == "__main__":
    try: main()
    finally: sys.exit(0)
