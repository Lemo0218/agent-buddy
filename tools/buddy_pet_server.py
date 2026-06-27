#!/usr/bin/env python3
"""Agent Buddy — web control panel for the desk pet (Tamagotchi mode).

A tiny dependency-free web app (stdlib http.server) that talks to the board
over USB serial. Open http://localhost:8787 in a browser. Buttons send pet-care
commands ({"cmd":"feed"} ...); the page polls the board for live stats.

The serial port is opened only momentarily per request, so this coexists with
the Claude Code hooks (which also open it briefly).

Run:  /usr/bin/python3.10 tools/buddy_pet_server.py [--port /dev/ttyACM0] [--http 8787]
"""
import sys, glob, json, time, argparse, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    import serial
except ImportError:
    sys.exit("pyserial not installed. Use /usr/bin/python3.10 (has pyserial).")

PORT = None
LOCK = threading.Lock()   # serialize our own serial access

def find_port(explicit=None):
    if explicit: return explicit
    c = sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*"))
    return c[0] if c else None

def talk(cmd):
    """Send {"cmd":...}, return the board's pet-stats dict (or {})."""
    if not PORT: return {}
    for _ in range(4):                      # retry if the port is briefly busy
        try:
            with LOCK:
                s = serial.Serial(PORT, 115200, timeout=0.6, write_timeout=0.6)
                s.write(("\n" + json.dumps({"cmd": cmd}) + "\n").encode())
                s.flush()
                end = time.time() + 1.0
                while time.time() < end:
                    line = s.readline().decode("utf-8", "replace").strip()
                    if line.startswith("{") and '"pet"' in line:
                        s.close()
                        return json.loads(line).get("pet", {})
                s.close()
                return {}
        except Exception:
            time.sleep(0.08)
    return {}

PAGE = """<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Agent Buddy</title><style>
:root{color-scheme:dark}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,sans-serif;
background:radial-gradient(120% 120% at 50% 0%,#16324a,#0a141d);color:#eaf2f6;
min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:24px}
h1{font-size:20px;margin:8px 0 2px;letter-spacing:.5px}
.sub{color:#8fb3c8;font-size:12px;margin-bottom:18px}
.card{background:#10212e;border:1px solid #1d3a4a;border-radius:18px;padding:20px;
width:min(420px,92vw);box-shadow:0 10px 40px #0006}
.lvl{display:flex;justify-content:space-between;align-items:center;font-size:14px;color:#9fd}
.bar{height:12px;border-radius:8px;background:#0c1922;overflow:hidden;margin:6px 0 14px}
.bar>i{display:block;height:100%;border-radius:8px;transition:width .4s}
.row{display:flex;align-items:center;gap:10px;margin:10px 0}
.row b{width:74px;font-size:13px;color:#bcd}
.grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px;margin-top:18px}
button{font:inherit;font-size:15px;padding:14px 8px;border:0;border-radius:14px;
background:#1c5f7e;color:#fff;cursor:pointer;transition:transform .08s,background .15s}
button:hover{background:#2680a8}button:active{transform:scale(.94)}
button.warn{background:#7e4a1c}button.warn:hover{background:#a8631f}
button.life{background:#2e6e3a}button.life:hover{background:#3a8c49}
.state{text-align:center;font-size:13px;color:#9fb;margin-top:14px;min-height:18px}
.faint{color:#f88}
</style></head><body>
<h1>\U0001F916 Agent Buddy</h1><div class=sub>desk pet • Tamagotchi mode</div>
<div class=card>
 <div class=lvl><span id=lv>Lv.-</span><span id=age>-</span></div>
 <div class=bar><i id=xp style="width:0%;background:#49c6e5"></i></div>
 <div class=row><b>\U0001F356 Hunger</b><div class=bar style=flex:1><i id=hunger style=background:#ffaa46></i></div></div>
 <div class=row><b>❤️ Happy</b><div class=bar style=flex:1><i id=happy style=background:#ff6e96></i></div></div>
 <div class=row><b>⚡ Energy</b><div class=bar style=flex:1><i id=energy style=background:#78d2ff></i></div></div>
 <div class=row><b>\U0001FA79 Health</b><div class=bar style=flex:1><i id=health style=background:#7be08c></i></div></div>
 <div class=grid>
  <button onclick="act('feed')">\U0001F356 Feed</button>
  <button onclick="act('play')">\U0001F3BE Play</button>
  <button onclick="act('pet')">✋ Pet</button>
  <button onclick="act('sleep')">\U0001F634 Sleep</button>
  <button onclick="act('wake')">☀️ Wake</button>
  <button class=warn onclick="act('medicine')">\U0001F48A Cure</button>
 </div>
 <div class=grid style=grid-template-columns:1fr>
  <button class=life onclick="act('hatch')">\U0001F95A Hatch new egg</button>
 </div>
 <div class=state id=state></div>
</div>
<script>
function set(id,v){document.getElementById(id).style.width=Math.max(0,Math.min(100,v))+'%'}
function render(p){
 if(!p||p.life===undefined){document.getElementById('state').textContent='(board not responding)';return}
 document.getElementById('lv').textContent='Lv.'+p.level+'  ('+p.xp+' xp)';
 document.getElementById('age').textContent=Math.floor(p.age_min/60)+'h '+(p.age_min%60)+'m old';
 set('xp',p.xp%100);set('hunger',p.hunger);set('happy',p.happy);set('energy',p.energy);set('health',p.health);
 const tiers=['baby','child','teen','adult'];
 const s=document.getElementById('state');
 if(p.life==='faint'){s.innerHTML='<span class=faint>\U0001F480 ran away... hatch a new egg</span>'}
 else if(p.life==='sick'){s.textContent='\U0001F912 sick — give medicine!'}
 else if(p.life==='sleep'){s.textContent='\U0001F634 sleeping...'}
 else{s.textContent='\U0001F44C '+tiers[p.tier||0]+' • feeling fine'}
}
async function act(c){const r=await fetch('/api/cmd?do='+c,{method:'POST'});render(await r.json())}
async function poll(){try{render(await (await fetch('/api/stats')).json())}catch(e){}}
setInterval(poll,2500);poll();
</script></body></html>"""

class H(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="application/json"):
        b = body.encode("utf-8")
        self.send_response(code); self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b))); self.end_headers()
        self.wfile.write(b)
    def log_message(self, *a): pass
    def do_GET(self):
        if self.path == "/" or self.path.startswith("/index"):
            self._send(200, PAGE, "text/html; charset=utf-8")
        elif self.path.startswith("/api/stats"):
            self._send(200, json.dumps(talk("stats")))
        else:
            self._send(404, "{}")
    def do_POST(self):
        if self.path.startswith("/api/cmd"):
            from urllib.parse import urlparse, parse_qs
            q = parse_qs(urlparse(self.path).query)
            do = (q.get("do") or ["stats"])[0]
            self._send(200, json.dumps(talk(do)))
        else:
            self._send(404, "{}")

def main():
    global PORT
    ap = argparse.ArgumentParser()
    ap.add_argument("--port"); ap.add_argument("--http", type=int, default=8787)
    a = ap.parse_args()
    PORT = find_port(a.port)
    print(f"Board serial: {PORT or '(none found)'}")
    print(f"Open  http://localhost:{a.http}  in your browser")
    ThreadingHTTPServer(("0.0.0.0", a.http), H).serve_forever()

if __name__ == "__main__":
    main()
