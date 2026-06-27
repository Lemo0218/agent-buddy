#!/usr/bin/env python3
"""Agent Buddy — PC-side NDJSON event sender / demo driver.

This is a stand-in for the future `desktoy-agent` bridge: it pushes one
NDJSON event per line over USB-CDC serial and echoes whatever the device
prints back (acks + the ASCII face when no LCD is attached).

Examples
--------
  # Run the scripted demo (idle -> thinking -> coding -> testing -> done ...)
  python3 send_event.py --port /dev/ttyACM0 demo

  # Send one event
  python3 send_event.py -p /dev/ttyACM0 event coding \
      --text "Editing files" --mood focus --motion nod --ttl 8000

  # Send a raw JSON line
  python3 send_event.py -p /dev/ttyACM0 raw '{"v":1,"event":"error","mood":"sad"}'

Note: requires pyserial (`pip install pyserial`).
"""
import argparse
import json
import sys
import threading
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial not installed. Run: pip install pyserial")


def reader_thread(ser, stop):
    """Continuously print everything the device sends back."""
    while not stop.is_set():
        try:
            line = ser.readline()
        except Exception:
            break
        if line:
            print("  <- " + line.decode("utf-8", "replace").rstrip())


def send(ser, obj):
    payload = json.dumps(obj, ensure_ascii=False)
    print("  -> " + payload)
    ser.write((payload + "\n").encode("utf-8"))
    ser.flush()


# A realistic AI-coding-session timeline for demos / screen recordings.
DEMO = [
    ({"event": "starting", "text": "Booting up",     "mood": "happy",     "motion": "nod"},    1.5),
    ({"event": "thinking", "text": "Reading code",   "mood": "focus",     "motion": "tilt"},   2.5),
    ({"event": "coding",   "text": "Editing files",  "mood": "focus",     "motion": "nod"},    3.0),
    ({"event": "testing",  "text": "Running tests",  "mood": "focus",     "motion": "none"},   2.5),
    ({"event": "error",    "text": "1 test failed",  "mood": "sad",       "motion": "shake"},  2.5),
    ({"event": "coding",   "text": "Fixing it",      "mood": "focus",     "motion": "nod"},    2.5),
    ({"event": "testing",  "text": "Re-running",     "mood": "focus",     "motion": "none"},   2.0),
    ({"event": "done",     "text": "All green!",     "mood": "happy",     "motion": "bounce"}, 3.0),
    ({"event": "message",  "text": "Need your input","mood": "surprised", "motion": "tilt"},   3.0),
    ({"event": "idle",     "text": "",               "mood": "neutral",   "motion": "none"},   1.0),
]


def run_demo(ser):
    print("Running demo sequence (Ctrl-C to stop)...")
    for obj, hold in DEMO:
        send(ser, {"v": 1, **obj, "ttl_ms": int(hold * 1000) + 500})
        time.sleep(hold)
    print("Demo complete.")


def main():
    ap = argparse.ArgumentParser(description="Agent Buddy event sender")
    ap.add_argument("-p", "--port", default="/dev/ttyACM0")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("demo", help="run the scripted demo timeline")

    ev = sub.add_parser("event", help="send one event")
    ev.add_argument("name", help="idle|starting|thinking|coding|testing|done|error|message")
    ev.add_argument("--text", default="")
    ev.add_argument("--mood", default="neutral")
    ev.add_argument("--motion", default="none")
    ev.add_argument("--ttl", type=int, default=0, help="ttl_ms (0 = until next)")

    rw = sub.add_parser("raw", help="send a raw JSON line")
    rw.add_argument("json", help="raw JSON object string")

    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.3)
    time.sleep(0.4)  # let USB-CDC settle

    stop = threading.Event()
    t = threading.Thread(target=reader_thread, args=(ser, stop), daemon=True)
    t.start()

    try:
        if args.cmd == "demo":
            run_demo(ser)
        elif args.cmd == "event":
            send(ser, {"v": 1, "event": args.name, "text": args.text,
                       "mood": args.mood, "motion": args.motion, "ttl_ms": args.ttl})
        elif args.cmd == "raw":
            print("  -> " + args.json)
            ser.write((args.json + "\n").encode("utf-8"))
            ser.flush()
        time.sleep(0.8)  # catch the device's reply before exiting
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        ser.close()


if __name__ == "__main__":
    main()
