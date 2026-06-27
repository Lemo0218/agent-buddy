#!/usr/bin/env bash
# Agent Buddy — one-shot setup: deps -> build -> flash -> Claude Code hooks.
# Plug the XIAO ESP32-S3 in (data USB cable) before running.
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

say() { printf "\n\033[1;36m==> %s\033[0m\n" "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }

# --- pick a Python and ensure pyserial -------------------------------
PY="${PYTHON:-python3}"
have "$PY" || { echo "Python 3 is required."; exit 1; }
say "Installing Python deps (pyserial) for $PY"
"$PY" -m pip install --user --quiet pyserial 2>/dev/null \
  || "$PY" -m pip install --user --break-system-packages --quiet pyserial

# --- PlatformIO ------------------------------------------------------
if have pio; then PIO=pio
elif [ -x "$HOME/.local/bin/pio" ]; then PIO="$HOME/.local/bin/pio"
else
  say "Installing PlatformIO"
  "$PY" -m pip install --user --quiet platformio \
    || "$PY" -m pip install --user --break-system-packages --quiet platformio
  PIO="$HOME/.local/bin/pio"
fi

# --- ccusage (usage data) -------------------------------------------
if ! have ccusage; then
  if have npm; then say "Installing ccusage (npm -g)"; npm install -g ccusage || true
  else echo "NOTE: Node.js/npm not found — install it then 'npm i -g ccusage' for usage gauges."; fi
fi

# --- build + flash ---------------------------------------------------
say "Building + flashing firmware (board must be connected)"
"$PIO" run -t upload

# --- wire up Claude Code hooks --------------------------------------
say "Configuring Claude Code hooks (~/.claude/settings.json)"
SES="$PY $DIR/tools/buddy_sessions.py"
"$PY" - "$SES" <<'PYEOF'
import json, os, sys
ses = sys.argv[1]
p = os.path.expanduser("~/.claude/settings.json")
os.makedirs(os.path.dirname(p), exist_ok=True)
try:
    with open(p) as f: d = json.load(f)
except Exception:
    d = {}
hooks = d.setdefault("hooks", {})
plan = {"UserPromptSubmit": "thinking", "Stop": "done", "Notification": "waiting", "SessionEnd": "end"}
for ev, state in plan.items():
    arr = hooks.setdefault(ev, [])
    if any("buddy_sessions" in h.get("command", "") for g in arr for h in g.get("hooks", [])):
        continue
    arr.append({"hooks": [{"type": "command", "command": f"{ses} {state}", "timeout": 5000}]})
with open(p, "w") as f: json.dump(d, f, indent=2)
print("  hooks wired:", ", ".join(plan))
PYEOF

cat <<EOF

✅ Done. Next:

  # start the background helpers
  $PY $DIR/tools/buddy_usage.py --watch 60 --budget 30 --block-budget 30 &
  $PY $DIR/tools/buddy_pet_server.py &     # web panel: http://localhost:8787

Then open Claude Code (a NEW session) and the buddy will react.
Tune --budget / --block-budget to match what you see in Claude's /usage.
EOF
