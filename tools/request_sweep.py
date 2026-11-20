#!/usr/bin/env python3
"""Force C2C passenger requests one by one and report what each one hit.

    tools/request_sweep.py [-j N] [--zr-only|--idle] <course,set,record> ...
    tools/request_sweep.py [-j N] --type 10/3            # every record of that type / sub-kind (10/ = every sub-kind, all = everything)
    tools/request_sweep.py [-j N] --one-per-type         # the first record of each type / sub-kind
    tools/request_sweep.py [-j N] --holly2 "3,0;5,0"     # real Holly 2 stages: map node (1..15), request list (0..3)

Each run is a Mac game in its own home (build/request-sweep/<c>_<s>_<r>, a copy of the
owner's saves) on Clarissa's path (Beach, variant 5) with OR2_DEBUG_REQUEST=<c,s,r>,
ZR held from frame 1700, and the exit combination once the request's window should be
over (about 7 input frames per cs after GO, plus 15 s), so the game writes its
result log. Per run it prints: the request's final [req] score / hearts, request /
traffic / object / robot latches, the NAVI display's misses (a NAVI miss ends the HUD
for that frame: no request sprites, no TIME digits) and the traffic module's missing
services. Windows open small, tiled across the screen; don't type into them.
"""
import os, re, shutil, subprocess, sys, time, collections
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.environ.get("OR2_GAME_ROOT", "/Applications/OutRun2006 Coast 2 Coast.app/Contents/Resources/Game")
OWNER_SAVES = os.path.expanduser("~/Library/Application Support/OutRunMac/Saves")
EXE = ROOT + "/build/macos/mac/OutRun2006 Coast 2 Coast.app/Contents/MacOS/OutRun2006 Coast 2 Coast"
OUT = ROOT + "/build/request-sweep"
# Clarissa (Beach) from the title, 2 s apart; the race starts at about frame 1527.
PATH = "400-403:A,520-523:A,640-643:A,760-763:DOWN,880-883:A,1000-1003:A,1120-1123:A,1240-1243:A,1360-1363:A"

def catalogue():
    out = subprocess.run([sys.executable, ROOT + "/tools/request_catalogue.py", GAME, "-v"],
                         capture_output=True, text=True, check=True).stdout
    rows = []
    for l in out.splitlines():
        m = re.match(r"(\w+)/(\d+) course (\d+) (\S+) set (\d) record (\d+) cs (\d+)\.\.(\d+)", l)
        if m:
            rows.append(dict(kind=m[1] + "/" + m[2], course=int(m[3]), name=m[4], set=int(m[5]),
                             record=int(m[6]), start=int(m[7]), end=int(m[8])))
    return rows

# Holly 2 (MIX 2 COURSE) map: from the title to the map, then the node's moves (nav 5CE850).
HOLLY2 = "A A A DOWN RIGHT RIGHT RIGHT A"
NODE_MOVES = {0: "", 1: "RIGHT", 2: "RIGHT RIGHT", 3: "RIGHT RIGHT DOWN", 4: "RIGHT RIGHT RIGHT", 5: "RIGHT RIGHT RIGHT DOWN",
              6: "RIGHT RIGHT RIGHT DOWN DOWN", 7: "RIGHT RIGHT RIGHT RIGHT", 8: "RIGHT RIGHT RIGHT RIGHT DOWN",
              9: "RIGHT RIGHT RIGHT RIGHT DOWN DOWN", 10: "RIGHT RIGHT RIGHT RIGHT DOWN DOWN DOWN",
              11: "RIGHT RIGHT RIGHT RIGHT RIGHT", 12: "RIGHT RIGHT RIGHT RIGHT RIGHT DOWN",
              13: "RIGHT RIGHT RIGHT RIGHT RIGHT DOWN DOWN", 14: "RIGHT RIGHT RIGHT RIGHT RIGHT DOWN DOWN DOWN",
              15: "RIGHT RIGHT RIGHT RIGHT RIGHT DOWN DOWN DOWN DOWN"}
def presses(words):
    f = 400; out = []
    for w in words.split():
        out.append("%d-%d:%s" % (f, f + 3, w)); f += 120
    return ",".join(out), f

def run(row, slot, hold):
    key = row.get("key") or "%d_%d_%d" % (row["course"], row["set"], row["record"])
    home = OUT + "/" + key; sg = home + "/SaveGame"
    shutil.rmtree(home, ignore_errors=True); os.makedirs(sg)
    for f in ("License1.dat", "common.dat", "rankings.dat", "options.ini"):
        if os.path.exists(OWNER_SAVES + "/" + f):
            shutil.copy(OWNER_SAVES + "/" + f, sg)
    os.symlink(OWNER_SAVES + "/OR2006C2C.cache", home + "/OR2006C2C.cache")
    end = row.get("end") or 400
    path, go = PATH, 1527
    if "node" in row:   # a real Holly 2 stage: the map node, A (its lists), Right x list, A, then car / gear / music
        tail = " A A A A" if row["node"] == 0 else " " + NODE_MOVES[row["node"]] + " A" + " RIGHT" * row["set"] + " A A A A"
        path, last = presses(HOLLY2 + tail)   # node 0: the whole course (A on it starts at once)
        go = last + 300
    exit_frame = go + end * 7 + 900   # about 9 cs per second from GO on stage 1
    script = path + ("" if hold == "idle" else ",%d-%d:ZR" % (go - 100, exit_frame - 60))
    steer = row.get("steer") or os.environ.get("SWEEP_STEER")   # e.g. "LEFT:90,NONE:60,RIGHT:90": a repeating pattern from GO
    if steer:
        pat = [(w.split(":")[0], int(w.split(":")[1])) for w in steer.split(",")]
        f, k, parts = go, 0, []
        while f < exit_frame - 60:
            b, n = pat[k % len(pat)]; k += 1
            if b != "NONE": parts.append("%d-%d:%s" % (f, min(f + n, exit_frame - 60), b))
            f += n
        script += "," + ",".join(parts)
    open(sg + "/autoplay.txt", "w").write("script=%s\nframes=%d\n" % (script, exit_frame))
    x, y = (slot % 4) * 330, 40 + (slot // 4) * 260
    env = dict(os.environ, OR2_GAME_ROOT=GAME, OR2_TRACE="1", OR2_HOST_TRACE="1",
               OR2_HOST_WINDOW_RECT="%d,%d,320,240" % (x, y), OR2_HOST_WINDOW_TITLE=key)
    if "node" not in row: env["OR2_DEBUG_REQUEST"] = "%d,%d,%d" % (row["course"], row["set"], row["record"])
    log = open(home + "/run.log", "w")
    p = subprocess.Popen([EXE, "--home=" + home], env=env, stdout=log, stderr=subprocess.STDOUT)
    try:
        p.wait(timeout=exit_frame / 50 + 120)
    except subprocess.TimeoutExpired:
        p.kill(); p.wait()
    return key, report(home)

def report(home):
    r = open(home + "/run.log", errors="ignore").read()
    try:
        res = open(home + "/SaveGame/OutRunMac.log", errors="ignore").read()
    except FileNotFoundError:
        res = ""
    out = []
    reqs = re.findall(r"^\[req\] .*$", r, re.M)
    dbg = re.search(r"^\[dbg\] request: .*$", r, re.M)
    out.append(dbg.group(0)[6:] if dbg else "no [dbg] request line")
    active = [l for l in reqs if "active" in l]
    out.append("final: " + (reqs[-1] if reqs else "no [req] lines") + ("" if active else "  (NEVER ACTIVE)"))
    for l in sorted(set(re.findall(r"^.*latched.*$", r, re.M)))[:6]:
        if "latched=0" not in l: out.append("  " + l[:240])
    seen = []
    for l in reqs:   # the forced request's own lines: its state changes
        k = re.sub(r"score=\d+.*", "", l)
        if not seen or seen[-1][0] != k: seen.append((k, l))
        else: seen[-1] = (k, l)
    out += ["  " + l[:200] for _, l in seen[:8]]
    m = re.search(r"^hud variant.*$", res, re.M)
    if m:
        hud = m.group(0)
        f = re.search(r"missing \(first frame\):(.*)", res); u = re.search(r"unmapped NAVI reads:(.*)", res)
        out.append("  NAVI: " + hud[hud.find("navi "):][:120])
        if f and f.group(1).strip(): out.append("  NAVI missing: " + f.group(1).strip()[:300])
        if u and u.group(1).strip(): out.append("  NAVI unmapped: " + u.group(1).strip()[:300])
    elif not res:
        out.append("  NO RESULT LOG (did not exit cleanly)")
    t = re.search(r"^race traffic .*$", res, re.M)
    if t:
        miss = t.group(0).split("missing:")[-1].strip()
        if miss: out.append("  traffic missing: " + miss[:300])
        lat = re.search(r"latched=1 fault=\S+ \(([^)]*)\)", t.group(0))
        if lat: out.append("  TRAFFIC LATCHED: " + lat.group(1)[:200])
        for k in ("OBJECTS LATCHED", "HEART LATCHED", "SCORE LATCHED"):
            i = t.group(0).find(k)
            if i >= 0: out.append("  " + t.group(0)[i:i + 200])
    return "\n".join(out)

def main():
    args = sys.argv[1:]; jobs = 6; hold = "zr"; rows = []
    if "-j" in args:
        i = args.index("-j"); jobs = int(args[i + 1]); del args[i:i + 2]
    if "--idle" in args:
        args.remove("--idle"); hold = "idle"
    if "--zr-only" in args:
        args.remove("--zr-only")
    cat = None
    if "--type" in args:
        i = args.index("--type"); want = args[i + 1].lower(); del args[i:i + 2]
        cat = cat or catalogue(); rows += [r for r in cat if r["kind"] == want or (want.endswith("/") and r["kind"].startswith(want)) or want == "all"]
    if "--one-per-type" in args:
        args.remove("--one-per-type"); cat = cat or catalogue(); seen = set()
        for r in sorted(cat, key=lambda r: r["start"]):
            if r["kind"] not in seen: seen.add(r["kind"]); rows.append(r)
    if "--holly2" in args:   # --holly2 node,list[;node,list...]: real Holly 2 stages from the map
        i = args.index("--holly2"); spec = args[i + 1]; del args[i:i + 2]
        cat = cat or catalogue()
        for item in spec.split(";"):
            node, lst = map(int, item.split(","))
            steer = os.environ.get("SWEEP_STEER", "")
            tag = "".join(ch for ch in steer if ch.isalnum())[:24]
            rows.append(dict(kind="holly2", key="h2_%d_%d%s" % (node, lst, "_" + tag if tag else ""), node=node, course=-1, set=lst, record=0,
                             name="Holly 2 node %d list %d" % (node, lst), start=0, end=int(os.environ.get("SWEEP_END_CS", "650"))))
    for a in args:
        c, s, rec = map(int, a.split(","))
        cat = cat or catalogue()
        m = [r for r in cat if (r["course"], r["set"], r["record"]) == (c, s, rec)]
        rows.append(m[0] if m else dict(kind="?", course=c, set=s, record=rec, name="?", start=0, end=400))
    rows.sort(key=lambda r: r["kind"])
    print("%d runs, %d at a time" % (len(rows), jobs), flush=True)
    free = list(range(jobs))
    def task(row):
        slot = free.pop(0)
        try:
            key, rep = run(row, slot, hold)
        finally:
            free.append(slot)
        print("== %s %s %s set %d record %d (cs %s..%s)\n%s" % (row["kind"], key, row["name"], row["set"], row["record"],
              row.get("start"), row.get("end"), rep), flush=True)
    with ThreadPoolExecutor(jobs) as ex:
        list(ex.map(task, rows))

if __name__ == "__main__":
    main()
