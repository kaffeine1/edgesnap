import socket, json, time, os, sys
from PIL import Image, ImageChops
QMP = "/tmp/codex-aros/aros13/qmp.sock"
MON = "/Volumes/EXT/Macchine Virtuali/AROSOne_x64/aros-x64-monitor-edgesnap13.sock"
W, H = 1024, 768
class Q:
    def __init__(self):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); self.s.settimeout(5); self.s.connect(QMP)
        self.f = self.s.makefile("rw")
        self.f.readline()                       # greeting
        self.cmd({"execute": "qmp_capabilities"})
    def cmd(self, obj):
        self.f.write(json.dumps(obj) + "\n"); self.f.flush()
        while True:
            line = self.f.readline()
            if not line: return None
            d = json.loads(line)
            if "return" in d or "error" in d: return d
    def events(self, evs):
        return self.cmd({"execute": "input-send-event", "arguments": {"events": evs}})
    def move(self, x, y):
        self.events([{"type": "abs", "data": {"axis": "x", "value": int(x * 32767 / (W - 1))}},
                     {"type": "abs", "data": {"axis": "y", "value": int(y * 32767 / (H - 1))}}])
    def btn(self, down):
        self.events([{"type": "btn", "data": {"down": bool(down), "button": "left"}}])
    def dump(self, path):
        self.cmd({"execute": "human-monitor-command", "arguments": {"command-line": "screendump %s" % path}})
        for i in range(30):
            time.sleep(0.2)
            if os.path.exists(path) and os.path.getsize(path) >= W * H * 3: break
        png = path[:-4] + ".png"; Image.open(path).convert("RGB").save(png); return png
def drag(q, x0, y0, x1, y1, steps=40, hold=0.05, tag="drag"):
    q.move(x0, y0); time.sleep(0.3)
    q.btn(True); time.sleep(0.3)
    for i in range(1, steps + 1):
        x = x0 + (x1 - x0) * i / steps; y = y0 + (y1 - y0) * i / steps
        q.move(x, y); time.sleep(hold)
    time.sleep(0.8)
    held = q.dump("/tmp/codex-aros/%s-held.ppm" % tag)
    q.btn(False); time.sleep(1.5)
    after = q.dump("/tmp/codex-aros/%s-after.ppm" % tag)
    return held, after
def remnants(png, ref_png, box):
    a = Image.open(png).convert("RGB").crop(box); b = Image.open(ref_png).convert("RGB").crop(box)
    d = ImageChops.difference(a, b).convert("L").point(lambda v: 255 if v > 24 else 0)
    bbox = d.getbbox(); n = sum(1 for p in d.getdata() if p)
    return n, bbox

def go(q, x, y):
    q.move(x, y); time.sleep(0.15); q.move(x, y); time.sleep(0.15)
def press_at(q, x, y):
    q.events([{"type": "abs", "data": {"axis": "x", "value": int(x * 32767 / (W - 1))}},
              {"type": "abs", "data": {"axis": "y", "value": int(y * 32767 / (H - 1))}},
              {"type": "btn", "data": {"down": True, "button": "left"}}])
def release_at(q, x, y):
    q.events([{"type": "abs", "data": {"axis": "x", "value": int(x * 32767 / (W - 1))}},
              {"type": "abs", "data": {"axis": "y", "value": int(y * 32767 / (H - 1))}},
              {"type": "btn", "data": {"down": False, "button": "left"}}])
def drag2(q, x0, y0, x1, y1, steps=40, hold=0.05, tag="drag"):
    go(q, x0, y0); press_at(q, x0, y0); time.sleep(0.3)
    for i in range(1, steps + 1):
        go(q, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps); time.sleep(hold)
    time.sleep(0.8)
    held = q.dump("/tmp/codex-aros/%s-held.ppm" % tag)
    release_at(q, x1, y1); time.sleep(1.5)
    after = q.dump("/tmp/codex-aros/%s-after.ppm" % tag)
    return held, after

import numpy as np
AX, BX, AY, BY = 1.1067, -57.0, 1.0814, -27.7      # measured: actual = A * cmd + B
def find_pointer(png, near=None, radius=120):
    im = np.array(Image.open(png).convert("RGB")).astype(int)
    r, g, b = im[:, :, 0], im[:, :, 1], im[:, :, 2]
    red = (r > 170) & (g < 90) & (b < 90)
    if near is not None:
        x0, y0 = near
        mask = np.zeros_like(red); ys = slice(max(0, y0 - radius), y0 + radius); xs = slice(max(0, x0 - radius), x0 + radius)
        mask[ys, xs] = True; red = red & mask
    ys, xs = np.nonzero(red)
    if len(xs) < 4: return None
    return int(xs.min()), int(ys.min())          # the arrow's tip is its top-left pixel
def goto(q, x, y, tries=5, tol=3):
    cx, cy = (x - BX) / AX, (y - BY) / AY
    for t in range(tries):
        cx = min(max(cx, 0), W - 1); cy = min(max(cy, 0), H - 1)
        go(q, cx, cy); time.sleep(0.35)
        png = q.dump("/tmp/codex-aros/goto.ppm")
        p = find_pointer(png, near=(int(x), int(y)))
        if p is None: p = find_pointer(png)
        if p is None: return None
        dx, dy = x - p[0], y - p[1]
        if abs(dx) <= tol and abs(dy) <= tol: return p
        cx += dx / AX; cy += dy / AY
    return p
def drag3(q, x0, y0, x1, y1, steps=30, hold=0.05, tag="drag"):
    p = goto(q, x0, y0); print("press at", p)
    q.btn(True); time.sleep(0.3)
    for i in range(1, steps + 1):
        x = x0 + (x1 - x0) * i / steps; y = y0 + (y1 - y0) * i / steps
        go(q, (x - BX) / AX, (y - BY) / AY); time.sleep(hold)
    time.sleep(0.8)
    held = q.dump("/tmp/codex-aros/%s-held.ppm" % tag)
    q.btn(False); time.sleep(1.5)
    after = q.dump("/tmp/codex-aros/%s-after.ppm" % tag)
    return held, after
