#!/usr/bin/env python3
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Drive the running OS4 QEMU VM from the host, through its monitor
# socket - so an install can be walked through and read back without a
# hand on the mouse.
#
# The two facts that make this work:
#   * the VM has a RELATIVE PS/2 mouse (this image cannot use usb-tablet:
#     OS4's hid.usbfd Guru-meditates on it), so absolute coordinates are
#     meaningless and pointer moves must be closed-loop;
#   * the pointer is a hardware sprite, invisible to a naive diff, but it
#     IS in the framebuffer - jiggle it and diff two screendumps and its
#     position falls out. moveto() then creeps up on the target.
#
# Monitor gotchas paid for once: the syntax is
# "screendump <file> [-f png]" (file FIRST), the command line has a
# length limit so keep paths short, and NEVER "system_reset" this VM -
# with -kernel bboot the reset hangs QEMU with the monitor unreachable;
# kill it and use scripts/run-os4-cocoa.sh instead.
#
#   python3 scripts/os4-vm.py shot /tmp/screen.png
#   python3 scripts/os4-vm.py click 265 96
#   python3 scripts/os4-vm.py type "C:EdgeSnap QUIT"

import os, socket, subprocess, sys, time

MON = "/Volumes/EXT/Macchine Virtuali/Amiga/emu/telegram-amiga/os4/qemu-monitor-2223.sock"
TMP = "/tmp"

class VM(object):
    def __init__(self):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(8); self.s.connect(MON); time.sleep(0.2)
        try: self.s.recv(65536)
        except Exception: pass

    def cmd(self, c, w=0.03):
        self.s.sendall((c + "\n").encode()); time.sleep(w)
        try: self.s.recv(65536)
        except Exception: pass

    # --- screen -----------------------------------------------------
    def dump(self, path):
        if os.path.exists(path): os.remove(path)
        self.cmd("screendump %s" % path, 0.05)
        for _ in range(40):
            if os.path.exists(path) and os.path.getsize(path) > 1000:
                time.sleep(0.15); return path
            time.sleep(0.1)
        raise RuntimeError("screendump failed: " + path)

    def png(self, out, width=1280):
        p = self.dump(TMP + "/vmshot.ppm")
        subprocess.run(["sips", "-s", "format", "png", "--resampleWidth",
                        str(width), p, "--out", out],
                       capture_output=True)
        return out

    def region(self, out, x, y, w, h, zoom=2):
        """Crop the live framebuffer to a rectangle and write a PNG."""
        pw, ph, d = self._read(self.dump(TMP + "/vmreg.ppm"))
        x = max(0, min(x, pw - 1)); y = max(0, min(y, ph - 1))
        w = min(w, pw - x); h = min(h, ph - y)
        rows = []
        for yy in range(y, y + h):
            o = (yy * pw + x) * 3
            rows.append(d[o:o + w * 3])
        tmp = TMP + "/vmcrop.ppm"
        f = open(tmp, "wb")
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(b"".join(rows)); f.close()
        subprocess.run(["sips", "-s", "format", "png", "--resampleWidth",
                        str(w * zoom), tmp, "--out", out], capture_output=True)
        return out

    # --- pointer ----------------------------------------------------
    def _read(self, path):
        f = open(path, "rb")
        assert f.readline().strip() == b"P6"
        l = f.readline()
        while l.startswith(b"#"): l = f.readline()
        w, h = map(int, l.split()); f.readline()
        return w, h, f.read()

    def locate(self, jig=10):
        """Return (x, y) of the pointer's hot spot, pointer left where found."""
        a = self._read(self.dump(TMP + "/pa.ppm"))
        self.cmd("mouse_move %d %d" % (jig, jig), 0.25)
        b = self._read(self.dump(TMP + "/pb.ppm"))
        self.cmd("mouse_move %d %d" % (-jig, -jig), 0.25)
        w, h, da = a[0], a[1], a[2]; db = b[2]
        pts = []
        for i in range(0, min(len(da), len(db)), 3):
            if da[i:i+3] != db[i:i+3]:
                p = i // 3; pts.append((p % w, p // w))
        if not pts: return None
        # cluster on a coarse grid: the blinking Shell cursor is noise
        cells = {}
        for (x, y) in pts: cells.setdefault((x // 24, y // 24), []).append((x, y))
        best, bestn = None, 0
        for k, v in cells.items():
            n = len(v)
            # a pointer jiggle changes a few hundred pixels; take the biggest
            if n > bestn: best, bestn = v, n
        xs = [p[0] for p in best]; ys = [p[1] for p in best]
        return (min(xs), min(ys))

    def moveto(self, tx, ty, tol=2, tries=14):
        for _ in range(tries):
            pos = self.locate()
            if pos is None:
                self.cmd("mouse_move -128 -128", 0.05); continue
            dx, dy = tx - pos[0], ty - pos[1]
            if abs(dx) <= tol and abs(dy) <= tol: return pos
            # Intuition accelerates big deltas, so close in with small
            # steps: far away move fast, near the target creep.
            step = 24 if (abs(dx) > 40 or abs(dy) > 40) else 3
            while dx or dy:
                sx = max(-step, min(step, dx)); sy = max(-step, min(step, dy))
                self.cmd("mouse_move %d %d" % (sx, sy), 0.01)
                dx -= sx; dy -= sy
            time.sleep(0.25)
        return self.locate()

    def click(self, dbl=False):
        # Intuition wants a press it can see: a down/up pair sent back to
        # back through the monitor is sometimes swallowed.
        self.cmd("mouse_button 1", 0.12); self.cmd("mouse_button 0", 0.12)
        if dbl:
            self.cmd("mouse_button 1", 0.12); self.cmd("mouse_button 0", 0.12)
        time.sleep(0.5)

    def press(self, x, y):
        """Click a gadget: the first click may only activate the window,
        so send two singles far enough apart not to read as a double."""
        self.moveto(x, y)
        self.click(); time.sleep(0.9); self.click()
        time.sleep(0.6)

    def click_at(self, x, y, dbl=False):
        p = self.moveto(x, y); self.click(dbl); return p

    # --- keyboard ---------------------------------------------------
    KEYS = {" ": "spc", "\n": "ret", "-": "minus", "=": "equal",
            ".": "dot", ",": "comma", "/": "slash", ";": "semicolon",
            "'": "apostrophe", "[": "bracket_left", "]": "bracket_right",
            "\\": "backslash", "`": "grave_accent"}
    SHIFTED = {":": "semicolon", "_": "minus", "+": "equal", "?": "slash",
               '"': "apostrophe", "(": "9", ")": "0", "!": "1", "*": "8",
               "<": "comma", ">": "dot", "|": "backslash", "#": "3",
               "$": "4", "%": "5", "&": "7", "@": "2", "^": "6"}
    def key(self, k, w=0.05): self.cmd("sendkey %s" % k, w)
    def typ(self, text):
        for ch in text:
            if ch in self.KEYS: self.key(self.KEYS[ch])
            elif ch in self.SHIFTED: self.key("shift-" + self.SHIFTED[ch])
            elif ch.isupper(): self.key("shift-" + ch.lower())
            else: self.key(ch)

if __name__ == "__main__":
    vm = VM()
    a = sys.argv[1:]
    if a[0] == "where": print(vm.locate())
    elif a[0] == "move": print(vm.moveto(int(a[1]), int(a[2])))
    elif a[0] == "click": print(vm.click_at(int(a[1]), int(a[2])))
    elif a[0] == "dclick": print(vm.click_at(int(a[1]), int(a[2]), True))
    elif a[0] == "type": vm.typ(a[1])
    elif a[0] == "key": vm.key(a[1])
    elif a[0] == "shot": print(vm.png(a[1]))
