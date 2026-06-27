# 🦖 Agent Buddy

A tiny USB desk pet that shows what your AI coding agent (Claude Code) is doing —
in real time, on a little screen, so you don't have to keep typing `/usage` or
staring at the terminal.

- **Working / waiting / done** — a cute green dino reacts as Claude codes, finishes, or needs your input.
- **Tamagotchi mode** — feed/play with the pet; it levels up as you code, and gets tired/scruffy as usage climbs.
- **Multi-session "rooms"** — open several Claude Code windows and each becomes a little room with its own dino. Water fills each room by how much that session has used; the sky behind them darkens as your 5-hour limit window counts down.
- **Usage at a glance** — top gauges show 5-hour and daily usage; you never have to check `/usage` again.

It's a "dumb but charming" display device: all the AI logic stays on your PC, which
sends one tiny status line over USB. The board just makes it adorable.

---

## Hardware

| Part | Notes |
|------|-------|
| **Seeed Studio XIAO ESP32-S3** | 8 MB flash + 8 MB PSRAM, native USB-C |
| **ST7789V 240×320 SPI LCD** | e.g. Seengreat 2.0" — non-touch, 8-pin SPI |
| USB-C **data** cable | must carry data, not charge-only |
| (optional) micro servo | for head nods — firmware-ready, not required |

Wiring: see [`docs/WIRING.md`](docs/WIRING.md). Pins are defined in [`src/config.h`](src/config.h).

## Quick start

```bash
# 1. Prerequisites: git, Python 3, and Node.js (for ccusage)
git clone https://github.com/Lemo0218/agent-buddy.git
cd agent-buddy

# 2. One-shot setup: installs deps, builds + flashes firmware,
#    and wires up Claude Code hooks. Plug the board in first.
./install.sh

# 3. Start the background helpers (usage + web control panel)
python3 tools/buddy_usage.py --watch 60 --budget 30 --block-budget 30 &
python3 tools/buddy_pet_server.py &        # then open http://localhost:8787

# 4. Use Claude Code as normal — the buddy reacts automatically.
```

That's it. Open 2+ Claude Code windows to see the multi-room view.

## How it works

```
 Claude Code hooks ─▶ tools/buddy_sessions.py ─┐
 ccusage (usage)   ─▶ tools/buddy_usage.py    ─┤── USB-CDC (NDJSON) ─▶ ESP32-S3 ─▶ LCD
 web buttons       ─▶ tools/buddy_pet_server.py┘                         (dino + rooms + gauges)
```

- **Claude Code hooks** (`UserPromptSubmit`, `Stop`, `Notification`, `SessionEnd`) call
  `buddy_sessions.py`, which tracks each session by its `session_id` and pushes a
  per-session roster to the board.
- **`buddy_usage.py`** reads usage via [`ccusage`](https://github.com/ryoppippi/ccusage)
  (it parses Claude's local transcripts) and pushes daily cost, the 5-hour reset
  countdown, and each session's water level.
- **`buddy_pet_server.py`** is a dependency-free web panel to feed/play with the pet.

The board itself runs the pet simulation (hunger/energy/XP, saved to flash) and all
the rendering. Protocol details are in the source; everything is line-delimited JSON
over the USB serial port.

## What you see on the screen

**One session → Tamagotchi view**
- The dino, its mood (from pet stats), stat bars (hunger/happy/energy), level + XP.
- Top gauges: **5-hour usage** (top line) and **today usage** (below), green→amber→red.
- As usage climbs the dino looks progressively tired/scruffy ("worked hard with you").

**Two or more sessions → Rooms view**
- One room per session, each with a mini dino: typing at a desk (working), "!" above
  head (waiting for you), or Zzz (idle).
- **Water level** in each room = that session's share of today's usage (vs your budget).
- **Sky** behind the grid goes day → night as the 5-hour window elapses; sunrise = reset.

## Tools

| Script | Role |
|--------|------|
| `tools/buddy_sessions.py` | hook target — per-session state + roster (run by Claude Code) |
| `tools/buddy_usage.py` | daemon — daily/5h usage + per-session water (`--watch N`) |
| `tools/buddy_pet_server.py` | web control panel (feed/play/pet …) at `:8787` |
| `tools/buddy_bridge.py` | simple single-event sender (manual testing) |
| `tools/send_event.py` | demo sequencer for the status events |
| `tools/make_mascot.py` | regenerate the boot mascot image |

## Configuration

- **Budgets** (the gauges are vs a budget, since the plan's exact limit isn't exposed
  locally): tune with `--budget` (daily $) and `--block-budget` (5-hour $) on
  `buddy_usage.py`. Glance at `/usage` in Claude and set them to taste.
- **Pins / peripherals**: `src/config.h` (`BUDDY_HAS_DISPLAY`, `BUDDY_HAS_SERVO`, pin map).

## Troubleshooting

- **Board not detected** → use a USB **data** cable (not charge-only); check `ls /dev/ttyACM*`.
- **Nothing on the LCD** → confirm wiring against `docs/WIRING.md`; `BUDDY_HAS_DISPLAY` must be `1`.
- **No reactions** → hook changes take effect in a **new** Claude Code session.
- **Wrong colors / mirrored** → adjust `invert` / rotation in `src/display.cpp` (`LGFX` config).

## License

MIT — see [LICENSE](LICENSE).
