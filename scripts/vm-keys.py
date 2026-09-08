import sys, importlib.util, time, os
sys.path.insert(0, "/tmp")
spec = importlib.util.spec_from_file_location("vmhelp", "/tmp/vmhelp.py")
h = importlib.util.module_from_spec(spec); spec.loader.exec_module(h)
from PIL import Image
v = h.vm()
def shot(tag):
    p = "/tmp/codex-aros/inst-%s.ppm" % tag
    try: os.remove(p)
    except OSError: pass
    v.dump(p)
    for i in range(25):
        time.sleep(0.3)
        if os.path.exists(p) and os.path.getsize(p) >= 1024*768*3: break
    png = p[:-4] + ".png"
    Image.open(p).convert("RGB").save(png)
    return png
def typ(text, wait=0.5):
    h.typ_it(v, text); time.sleep(wait)
def key(k, wait=0.3):
    v.key(k, 0.1); time.sleep(wait)
