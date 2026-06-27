#!/usr/bin/env python3
"""Agent Buddy — push Claude usage to the desk pet (global gauge + per-session water + reset sky).

Every refresh it pushes:
  {"cmd":"usage","cost","tok","pct"}                     -> single-pet fuel gauge + tired dino
  {"cmd":"world","reset","rmin","rooms":[{f,c,...}]}     -> per-room water level + day/night sky

- Per-room water "f" = that session's cost / daily budget (computed from its transcript).
- Global "reset"/"rmin" = elapsed % and minutes-left of the active 5-hour block (ccusage blocks).

Run:  /usr/bin/python3.10 tools/buddy_usage.py --watch 60 [--budget 30]
"""
import sys, os, glob, json, time, argparse, subprocess, shutil, datetime, fcntl

STATE_FILE = os.path.expanduser("~/.claude/buddy_sessions.json")
STALE_SEC  = 20 * 60

def find_ccusage():
    p = shutil.which("ccusage")
    if p: return p
    for c in sorted(glob.glob("/home/mh/.nvm/versions/node/*/bin/ccusage"), reverse=True):
        return c
    return None

def find_port():
    c = sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*"))
    return c[0] if c else None

def today_usage(cc):
    try:
        d = json.loads(subprocess.run([cc,"daily","--json"],capture_output=True,text=True,timeout=90).stdout)
    except Exception: return None, None
    rows = d.get("daily", d if isinstance(d, list) else [])
    if not rows: return None, None
    today = datetime.date.today().isoformat()
    r = next((x for x in rows if x.get("date") == today), rows[-1])
    return float(r.get("totalCost", 0)), int(r.get("totalTokens", 0))

def reset_info(cc):
    """Return (elapsed_pct, minutes_left, block_cost) for the active 5h block."""
    try:
        d = json.loads(subprocess.run([cc,"blocks","--active","--json"],capture_output=True,text=True,timeout=90).stdout)
        b = (d.get("blocks") or [None])[0]
        if not b: return -1, -1, 0.0
        fmt = "%Y-%m-%dT%H:%M:%S.%f%z"
        st = datetime.datetime.strptime(b["startTime"].replace("Z","+0000"), fmt)
        en = datetime.datetime.strptime(b["endTime"].replace("Z","+0000"),   fmt)
        now = datetime.datetime.now(datetime.timezone.utc)
        total = (en - st).total_seconds()
        elapsed = max(0.0, min(1.0, (now - st).total_seconds() / total)) if total else 0
        return int(elapsed * 100), max(0, int((en - now).total_seconds() // 60)), float(b.get("costUSD", 0))
    except Exception:
        return -1, -1, 0.0

def session_today_tokens(transcript):
    """Deduped total tokens used by a session TODAY (local date). 0 if none."""
    if not transcript or not os.path.exists(transcript): return 0
    today = datetime.datetime.now().astimezone().date()
    seen = set(); tok = 0
    try:
        for ln in open(transcript, encoding="utf-8"):
            try: o = json.loads(ln)
            except Exception: continue
            m = o.get("message") or {}
            u = m.get("usage")
            if not u: continue
            mid = m.get("id") or o.get("uuid")
            if mid in seen: continue
            seen.add(mid)
            try:
                d = datetime.datetime.fromisoformat(o.get("timestamp","").replace("Z","+00:00")).astimezone().date()
            except Exception:
                continue
            if d == today:
                tok += (u.get("input_tokens",0)+u.get("output_tokens",0)
                        +u.get("cache_creation_input_tokens",0)+u.get("cache_read_input_tokens",0))
    except Exception:
        return 0
    return tok

def update_fills(budget, daily_cost):
    """Split today's authoritative ccusage cost across sessions by token share.
    Per-room cost sums to the real daily total; fill = cost / budget."""
    if not os.path.exists(STATE_FILE): return []
    f = open(STATE_FILE, "r+"); fcntl.flock(f, fcntl.LOCK_EX)
    try:
        try: data = json.loads(f.read() or "{}")
        except Exception: data = {}
        now = time.time()
        data = {k:v for k,v in data.items() if now - v.get("ts",0) < STALE_SEC}
        weights = {k: session_today_tokens(v.get("transcript","")) for k, v in data.items()}
        total_w = sum(weights.values())
        for k, v in data.items():
            share = (weights[k] / total_w) if total_w else 0.0
            c = (daily_cost or 0.0) * share
            v["cost"] = round(c, 2)
            v["fill"] = max(0, min(100, int(c / budget * 100))) if budget else 0
        f.seek(0); f.truncate(); f.write(json.dumps(data)); f.flush()
        rooms = sorted(data.values(), key=lambda v: v["ts"], reverse=True)[:6]
        return [{"st":r["st"], "p":(r.get("proj") or "")[:10],
                 "f":int(r.get("fill",0)), "c":round(r.get("cost",0.0),2)} for r in rooms]
    finally:
        fcntl.flock(f, fcntl.LOCK_UN); f.close()

def push(port, obj):
    import serial
    for _ in range(4):
        try:
            s = serial.Serial(port,115200,timeout=0.5,write_timeout=0.6)
            s.write(("\n"+json.dumps(obj)+"\n").encode()); s.flush(); s.close(); return True
        except Exception: time.sleep(0.08)
    return False

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--budget", type=float, default=30.0, help="daily $ budget for the gauge")
    ap.add_argument("--block-budget", type=float, default=30.0, help="5-hour block $ budget")
    ap.add_argument("--watch", type=int, default=0)
    a = ap.parse_args()
    cc = find_ccusage()
    if not cc: sys.exit("ccusage not found")

    def once():
        port = find_port()
        if not port: print("no board"); return
        cost, tok = today_usage(cc)
        rp, rm, bcost = reset_info(cc)
        rooms = update_fills(a.budget, cost)
        if cost is not None:
            pct  = max(0, min(100, int(cost / a.budget * 100)))
            bpct = max(0, min(100, int(bcost / a.block_budget * 100))) if a.block_budget else -1
            push(port, {"cmd":"usage","cost":round(cost,2),"tok":tok,"pct":pct,"bpct":bpct})
        push(port, {"cmd":"world","reset":rp,"rmin":rm,"rooms":rooms})
        print(f"{datetime.datetime.now():%H:%M:%S}  day ${cost or 0:.2f}({a.budget:.0f})  "
              f"5h ${bcost:.2f}({a.block_budget:.0f})  reset {rm}m  rooms={len(rooms)}")

    once()
    while a.watch:
        time.sleep(a.watch); once()

if __name__ == "__main__":
    main()
