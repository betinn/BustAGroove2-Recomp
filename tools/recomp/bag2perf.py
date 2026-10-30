"""Mede uma partida do BAG2 na build-debug (TCP 4370): fps do log + contadores hd_textures.
Uso: python bag2perf.py <rotulo> [segundos_de_partida=120]
"""
import json, os, socket, subprocess, sys, time
B = r"C:\Users\sem-m\source\GIT\BustAGroove2Web\tools\recomp\bag2\build-debug"
LOGS = r"C:\Users\sem-m\source\GIT\BustAGroove2Web\tools\recomp\logs"
DISC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'disc', 'Bust A Groove 2 (USA).cue')
label = sys.argv[1]; secs = int(sys.argv[2]) if len(sys.argv) > 2 else 120
_id = 0
def cmd(obj, timeout=5.0):
    global _id; _id += 1; obj = dict(obj); obj["id"] = _id
    with socket.create_connection(("127.0.0.1", 4370), timeout=timeout) as s:
        s.sendall((json.dumps(obj) + "\n").encode()); buf = b""
        while not buf.endswith(b"\n"):
            c = s.recv(65536)
            if not c: break
            buf += c
    return json.loads(buf.decode() or "{}")
def press(mask, frames=15): return cmd({"cmd": "press", "buttons": mask, "frames": frames})
START, CROSS = 0xFFF7, 0xBFFF
log = open(os.path.join(LOGS, f"perf-{label}.log"), "w")
env = dict(os.environ, PSX_FRAME_REPORT_MS="1000")
p = subprocess.Popen([os.path.join(B, "Bust_A_Groove_2_Recompiled.exe"), "--game", "game.toml", "--disc", DISC],
                     cwd=B, stdout=log, stderr=subprocess.STDOUT, env=env)
t0 = time.time()
def until(t):
    d = t0 + t - time.time()
    if d > 0: time.sleep(d)
until(10)
for _ in range(20):
    try: cmd({"cmd": "hd_textures"}); break
    except OSError: time.sleep(1)
print("hd:", json.dumps(cmd({"cmd": "hd_textures"}))[:300], flush=True)
until(75); press(START); until(78); press(CROSS); until(81); press(CROSS); until(84); press(CROSS)
until(96)
def lastfps():
    try:
        with open(os.path.join(LOGS, f"perf-{label}.log"), errors="replace") as f:
            lines = [l for l in f if "fps=" in l]
        return lines[-1].strip().replace("psxrecomp: ", "") if lines else "?"
    except OSError: return "?"
rows = []
tstart = time.time()
prev = cmd({"cmd": "hd_textures"})
while time.time() - tstart < secs:
    time.sleep(10)
    h = cmd({"cmd": "hd_textures"})
    fps = lastfps()
    d = {k: h[k] - prev.get(k, 0) for k, v in h.items() if isinstance(v, (int, float)) and not isinstance(v, bool)}
    prev = h
    keep = {k: d[k] for k in d if any(s in k for s in ("composition", "gl_", "decode", "matched", "ready", "applied", "draw", "upload", "evict"))}
    row = f"t+{int(time.time()-tstart):3d}s {fps} | delta10s {json.dumps(keep)}"
    print(row, flush=True); rows.append(row)
try: cmd({"cmd": "quit"})
except OSError: pass
try: p.wait(30)
except subprocess.TimeoutExpired: p.kill()
with open(os.path.join(LOGS, f"perf-{label}.summary.txt"), "w") as f: f.write("\n".join(rows) + "\n")
print("fim", label)
