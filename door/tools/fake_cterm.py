#!/usr/bin/env python3
"""
fake_cterm.py -- a fake CTerm terminal for trying the door's JPEG XL mode headless (on the development machine, never
on the BBS box). It runs duke3ddoor on a pseudo-terminal and:
  - answers who-are-you as CTerm 1.332 (--oldcterm: 1.331, which can't zoom, so frames come pre-scaled), Q;JXL,
    Q;libsndfile and the two formats, CSI < 0 c with feature 8 (key reports; --nokeys leaves it out), CSI ? 2;1 S as
    640x400, CSI 5 n, and every ESC [ 6 n at once;
  - keeps a file cache (C;S stores, C;L lists name TAB md5), so a second run finds the sounds already there;
  - decodes every DrawJXLBlob / DrawJXL onto a 640x400 canvas (ZX/ZY scaled) and saves PNG screenshots;
  - logs every sound command (A;...) to apc.log and sums them up;
  - plays a script of keys: ESC [ = <evdev> K / k with key reports, plain bytes without.
The link is modelled by the door itself: --kbps/--ping write saves/player/linktest.cfg, and the door's own
saves/player/jxl.log (fps, KB/s, distance, round trip every 5 s) is copied into OUTDIR.

    python3 tools/fake_cterm.py OUTDIR [--kbps 500 --ping 80] [--nokeys] [--oldcterm] [--keepcache]
"""
import argparse, base64, hashlib, os, pty, re, select, shutil, sys, time

import numpy as np, imagecodecs
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
DOOR_DIR = os.path.dirname(HERE)
DOOR = os.path.join(DOOR_DIR, "duke3ddoor")
PREFIX = "SyncTERM:"            # the protocol's fixed tag

ap = argparse.ArgumentParser()
ap.add_argument("outdir")
ap.add_argument("--kbps", type=int, default=0)
ap.add_argument("--ping", type=int, default=0)
ap.add_argument("--nokeys", action="store_true")
ap.add_argument("--oldcterm", action="store_true")
ap.add_argument("--keepcache", action="store_true", help="reuse OUTDIR/cache from an earlier run")
ap.add_argument("--menuwait", type=int, default=0, help="seconds to stay in the main menu (to hear its song loop)")
args = ap.parse_args()

out = args.outdir
os.makedirs(out, exist_ok=True)
cache_dir = os.path.join(out, "cache")
if not args.keepcache:
    shutil.rmtree(cache_dir, ignore_errors=True)
os.makedirs(cache_dir, exist_ok=True)

player = os.path.join(DOOR_DIR, "saves", "player")
os.makedirs(player, exist_ok=True)
for f in ("linktest.cfg", "display.cfg"):
    if os.path.exists(os.path.join(player, f)):
        os.remove(os.path.join(player, f))
if args.kbps:
    with open(os.path.join(player, "linktest.cfg"), "w") as f:
        f.write("kbps=%d\nping=%d\n" % (args.kbps, args.ping))

W, H = 640, 400
canvas = np.zeros((H, W, 3), np.uint8)
apc_log = open(os.path.join(out, "apc.log"), "w")
stats = {"bytes": 0, "pics": 0, "q6n": 0, "sfx_queued": 0, "sfx_loops": 0, "sfx_uploaded": 0, "mus_uploaded": 0,
         "mus_queued": 0, "volume": 0, "flush": 0}
songs = []

pid, fd = pty.fork()
if pid == 0:
    os.environ["DUKEDOOR_LOG"] = os.path.join(os.path.abspath(out), "game.log")
    os.execv(DOOR, [DOOR])

t0 = time.time()
buf = b""
seen = b""                  # the last of everything the door sent, for waiting on its words


def send(b):
    os.write(fd, b)


def cache_store(name, data):
    path = os.path.join(cache_dir, name.replace("/", "__"))
    with open(path, "wb") as f:
        f.write(data)


def cache_load(name):
    with open(os.path.join(cache_dir, name.replace("/", "__")), "rb") as f:
        return f.read()


def draw(data, opts):
    img = imagecodecs.jpegxl_decode(data)
    if img.ndim == 2:
        img = np.stack([img] * 3, -1)
    img = img[:, :, :3]
    zx, zy = opts.get("ZX", 1), opts.get("ZY", 1)
    if zx > 1 or zy > 1:
        img = np.repeat(np.repeat(img, zy, 0), zx, 1)
    h, w = img.shape[:2]
    x, y = opts.get("DX", 0), opts.get("DY", 0)
    h, w = min(h, H - y), min(w, W - x)
    canvas[y:y + h, x:x + w] = img[:h, :w]
    stats["pics"] += 1


def apc(cmd):
    if not cmd.startswith(PREFIX):
        return
    cmd = cmd[len(PREFIX):]
    if cmd == "Q;JXL":
        send(b"\x1b[=1;1-n")
    elif cmd == "Q;libsndfile":
        send(b"\x1b[=7;100;1n")
    elif cmd.startswith("Q;libsndfileFormat;"):
        a, b = cmd.split(";")[2:4]
        send(b"\x1b[=7;101;%s;%s;1n" % (a.encode(), b.encode()))
    elif cmd.startswith("C;L;"):
        prefix = cmd[4:].rstrip("*")
        lines = []
        for f in sorted(os.listdir(cache_dir)):
            name = f.replace("__", "/")
            if name.startswith(prefix):
                lines.append("%s\t%s" % (name[len(prefix):], hashlib.md5(cache_load(name)).hexdigest()))
        send(("\x1b_" + PREFIX + "C;L\n" + "".join(l + "\n" for l in lines) + "\x1b\\").encode())
    elif cmd.startswith("C;S;"):
        _, _, name, data = cmd.split(";", 3)
        cache_store(name, base64.b64decode(data))
        if "/sfx/" in name:
            stats["sfx_uploaded"] += 1
        if "/mus/" in name:
            stats["mus_uploaded"] += 1
            apc_log.write("%7.2f upload %s\n" % (time.time() - t0, name))
    elif cmd.startswith("C;DrawJXLBlob;"):
        parts = cmd.split(";")
        opts = {k: int(v) for k, v in (x.split("=") for x in parts[2:-1])}
        draw(base64.b64decode(parts[-1]), opts)
    elif cmd.startswith("C;DrawJXL;"):
        parts = cmd.split(";")
        opts = {k: int(v) for k, v in (x.split("=") for x in parts[2:-1])}
        draw(cache_load(parts[-1]), opts)
    elif cmd.startswith("A;"):
        apc_log.write("%7.2f %s\n" % (time.time() - t0, cmd))
        if cmd.startswith("A;Queue;"):
            if "C=2;" in cmd + ";":
                stats["mus_queued"] += 1
            else:
                stats["sfx_queued"] += 1
                if cmd.endswith(";L"):
                    stats["sfx_loops"] += 1
        elif cmd.startswith("A;Load;S=250;"):
            song = cmd.split("/")[-1].rsplit("_", 1)[0]
            if not songs or songs[-1] != song:
                songs.append(song)
        elif cmd.startswith("A;Volume"):
            stats["volume"] += 1
        elif cmd.startswith("A;Flush"):
            stats["flush"] += 1


def handle():
    global buf
    while True:
        i = buf.find(b"\x1b")
        if i < 0:
            buf = b""
            return
        buf = buf[i:]
        if buf.startswith(b"\x1b_"):
            j = buf.find(b"\x1b\\")
            if j < 0:
                return
            apc(buf[2:j].decode("latin1"))
            buf = buf[j + 2:]
            continue
        m = re.match(rb"\x1b\[([0-9;?=<>]*)([A-Za-z])", buf)
        if not m:
            if len(buf) < 32 and re.match(rb"\x1b(\[[0-9;?=<>]*)?$", buf):
                return
            buf = buf[1:]
            continue
        p, f = m.group(1), m.group(2)
        buf = buf[m.end():]
        if f == b"c" and p == b"":
            send(b"\x1b[=67;84;101;114;109;1;%dc" % (331 if args.oldcterm else 332))
        elif f == b"c" and p == b"<0":
            send(b"\x1b[<0;1;2;3;4;5;6;7c" if args.nokeys else b"\x1b[<0;1;2;3;4;5;6;7;8c")
        elif f == b"n" and p == b"5":
            send(b"\x1b[0n")
        elif f == b"n" and p == b"6":
            stats["q6n"] += 1
            send(b"\x1b[1;1R")
        elif f == b"S" and p == b"?2;1":
            send(b"\x1b[?2;0;%d;%dS" % (W, H))


def pump(secs):
    global buf, seen
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.005)
        if not r:
            continue
        try:
            d = os.read(fd, 1 << 20)
        except OSError:
            return False
        if not d:
            return False
        buf += d
        seen = (seen + d)[-4096:]
        stats["bytes"] += len(d)
        handle()
    return True


def shot(name):
    Image.fromarray(canvas).save(os.path.join(out, name + ".png"))
    print("%6.1f shot %-14s pics %5d  %s" % (time.time() - t0, name, stats["pics"],
          {k: v for k, v in stats.items() if k.startswith(("sfx", "mus"))}), flush=True)


# evdev key codes
ESC, ENTER, CTRL, SPACE, UP, LEFT, RIGHT, DOWN, Q = 1, 28, 29, 57, 103, 105, 106, 108, 16
PLAIN = {ESC: b"\x1b", ENTER: b"\r", SPACE: b" ", UP: b"\x1b[A", LEFT: b"\x1b[D", RIGHT: b"\x1b[C", DOWN: b"\x1b[B",
         CTRL: b"f"}


def tap(code, hold=0.12):
    if args.nokeys:
        send(PLAIN[code])
        pump(hold)
    else:
        send(b"\x1b[=%dK" % code)
        pump(hold)
        send(b"\x1b[=%dk" % code)


def hold(code, secs):
    """With key reports one press and release; without, the key's repeats as a terminal sends them"""
    if args.nokeys:
        send(PLAIN[code])
        pump(0.5)
        end = time.time() + secs - 0.5
        while time.time() < end:
            send(PLAIN[code])
            pump(0.033)
    else:
        send(b"\x1b[=%dK" % code)
        pump(secs)
        send(b"\x1b[=%dk" % code)


# The start page: TRACE's question goes unanswered (half a second), then the CTerm ones, then the menu
pump(3)
send(b"2")
pump(1)
for _ in range(240):                     # the effects go up the first time, then "Press any key to start"
    if b"Press any key to start" in seen or not pump(0.25):
        break
print("%6.1f start page: %d effects uploaded" % (time.time() - t0, stats["sfx_uploaded"]), flush=True)
send(b" ")
start = time.time()
pump(3)
shot("a_logo")
pump(6)
shot("b_title")
pump(4)
tap(ESC)
pump(1.5)
shot("c_menu")
if args.menuwait:
    pump(args.menuwait)
tap(ENTER)
pump(1.5)
shot("d_episode")
tap(ENTER)
pump(1.5)
shot("e_skill")
tap(ENTER)
pump(4)
shot("f_ingame")
hold(UP, 2.0)
pump(0.3)
shot("g_walked")
tap(CTRL)
pump(0.4)
tap(CTRL)
pump(0.3)
shot("h_fired")
hold(RIGHT, 0.6)
pump(0.5)
shot("i_turned")

# Moving for 15 s (turning and walking: nearly every tile changes), for the frame rate on the modelled link
pics0, t_move = stats["pics"], time.time()
for i in range(5):
    hold(UP, 1.5)
    hold(LEFT if i % 2 else RIGHT, 1.5)
moving_secs = time.time() - t_move
shot("j_moved")
tap(ESC)
pump(1.5)
shot("k_esc_menu")
tap(ESC)
pump(1)

# Back to the BBS with Ctrl-Q
if args.nokeys:
    send(b"\x11")
else:
    send(b"\x1b[=29K\x1b[=16K")
alive = pump(4)
send(b" ")
pump(1)
apc_log.close()
try:
    os.kill(pid, 9)
except Exception:
    pass
for f in ("jxl.log",):
    if os.path.exists(os.path.join(player, f)):
        shutil.copy(os.path.join(player, f), out)
print("moving %.1f s: %d pictures" % (moving_secs, stats["pics"] - pics0))
print("songs played:", songs)
print("totals:", stats)
