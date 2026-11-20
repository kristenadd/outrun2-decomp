#!/usr/bin/env python3
"""Drive the Mac game with autoplay.txt and screenshot it at given input frames.

    tools/autoplay_shots.py <name> "@A A RIGHT ..." [frame,frame,...] [ENV=value ...]
    tools/autoplay_shots.py <name> "400-403:A,520-523:RIGHT" frame,frame,...

"@..." form: one press every 120 input frames (2 s) from frame 400, a screenshot
100 frames after each press. "300*" waits 300 more frames; "ZR:600" holds ZR for
600 frames. Prints the generated script, then "<frame> <trace line>" per shot.

Runs in its own home (build/autoplay-home[X]/SaveGame, a copy of the owner's
saves made on first use), so the owner's saves and options are never touched.
EXIT_FRAME=<n> exits the game cleanly at input frame n (L+R+Plus), so it writes its result
log (<home>/<log file>, with the traffic / request summary). RUN_X=<x> places the window (and the capture) at x, for two runs side by side
(RUN_X=0 and RUN_X=820). Shots and run.log go to build/autoplay-shots/<name>/.
"""
import os, re, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = "/Applications/OutRun2006 Coast 2 Coast.app/Contents/Resources/Game"
OWNER_SAVES = os.path.expanduser("~/Library/Application Support/OutRunMac/Saves")
EXE = ROOT + "/build/macos/mac/OutRun2006 Coast 2 Coast.app/Contents/MacOS/OutRun2006 Coast 2 Coast"

X = int(os.environ.get("RUN_X", "0"))
H = ROOT + "/build/autoplay-home" + ("" if X == 0 else str(X))
SG = H + "/SaveGame"
if not os.path.isdir(SG):
    os.makedirs(SG)
    for f in ("License1.dat", "common.dat", "rankings.dat", "options.ini"):
        if os.path.exists(OWNER_SAVES + "/" + f):
            shutil.copy(OWNER_SAVES + "/" + f, SG)
    os.symlink(OWNER_SAVES + "/OR2006C2C.cache", H + "/OR2006C2C.cache")

name, script = sys.argv[1], sys.argv[2]
extra = [a for a in sys.argv[3:] if "=" not in a]
if script.startswith("@"):
    f = 400; items = []; shots = []
    for t in script[1:].split():
        if t.endswith("*"):
            f += int(t[:-1]); continue
        b, _, hold = t.partition(":"); hold = int(hold or 3)
        items.append(f"{f}-{f+hold}:{b}"); shots.append(f + max(100, hold + 20)); f += max(120, hold + 40)
    script = ",".join(items)
    if extra:
        shots = sorted(set(shots + [int(x) for x in extra[0].split(",")]))
    print(script, flush=True)
else:
    shots = [int(x) for x in extra[0].split(",")]

env = dict(os.environ, OR2_GAME_ROOT=GAME, OR2_HOST_WINDOW_RECT=f"{X},40,800,600", OR2_HOST_TRACE="1")
for a in sys.argv[3:]:
    if "=" in a:
        k, v = a.split("=", 1); env[k] = v
EXIT = os.environ.get("EXIT_FRAME")   # EXIT_FRAME=N: the exit combination at N, then wait for a clean exit (writes the result log)
open(SG + "/autoplay.txt", "w").write("trace=1\nscript=" + script + "\n" + (f"frames={EXIT}\n" if EXIT else ""))
if os.path.exists(SG + "/trace.txt"):
    os.remove(SG + "/trace.txt")
out_dir = ROOT + "/build/autoplay-shots/" + name
os.makedirs(out_dir, exist_ok=True)
log = open(out_dir + "/run.log", "w")
p = subprocess.Popen([EXE, "--home=" + H], env=env, stdout=log, stderr=subprocess.STDOUT)

def last():
    try:
        with open(SG + "/trace.txt", "rb") as f:
            f.seek(0, 2); n = f.tell(); f.seek(max(0, n - 4000))
            ls = [l for l in f.read().decode(errors="ignore").splitlines() if l.startswith("f=")]
            return ls[-1] if ls else None
    except FileNotFoundError:
        return None

t0 = time.time(); l = None
try:
    for fr in shots:
        while True:
            l = last()
            if l and int(re.match(r"f=(\d+)", l).group(1)) >= fr: break
            if p.poll() is not None or time.time() - t0 > float(os.environ.get("RUN_TIMEOUT", "900")): break
            time.sleep(0.05)
        shot = f"{out_dir}/{fr}.jpg"
        # The window's content starts 28 points under its top (title bar).
        subprocess.run(["screencapture", "-x", f"-R{X},68,800,600", "-t", "jpg", shot])
        subprocess.run(["sips", "-Z", "480", shot], capture_output=True)
        print(fr, l, flush=True)
finally:
    if EXIT:
        try: p.wait(timeout=120)
        except subprocess.TimeoutExpired: pass
    p.kill(); p.wait()
    os.remove(SG + "/autoplay.txt")
