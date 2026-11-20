# Real keyboard input (SendInput scan codes) for the release build in fullscreen, where the test harness is not loaded.
# Sends only while the window titled exactly TITLE is the foreground window; otherwise aborts.
#   python tools/sk.py KEY [KEY..]     keys as in ed.py (esc enter up down ... a..z 0..9), "w500" waits 500 ms
#   python tools/sk.py hold KEY MS
import sys, os, time, ctypes, ctypes.wintypes as W
sys.path.insert(0, os.path.dirname(__file__))
from wl import wins
from ed import DIK
TITLE = os.environ.get('EDJP_TITLE', 'Elite - Dangerous (CLIENT)')
u = ctypes.windll.user32

class KI(ctypes.Structure):
    _fields_ = [('vk', W.WORD), ('sc', W.WORD), ('fl', W.DWORD), ('t', W.DWORD), ('ex', ctypes.c_size_t)]
class INP(ctypes.Structure):
    _fields_ = [('type', W.DWORD), ('ki', KI), ('pad', ctypes.c_byte * 8)]

def guard():
    w = [x for x in wins(title=TITLE) if x[5]]
    if not w or u.GetForegroundWindow() != w[0][0]:
        raise SystemExit('game window "%s" is not in front; not sending keys' % TITLE)

def send(sc, up):
    fl = 0x8 | (0x2 if up else 0)
    if sc & 0x80: fl |= 0x1; sc &= 0x7f
    i = INP(1, KI(0, sc, fl, 0, 0))
    u.SendInput(1, ctypes.byref(i), ctypes.sizeof(i))

def tap(k, ms=80):
    guard(); d = DIK[k]
    send(d, False); time.sleep(ms / 1000); send(d, True); time.sleep(0.25)

if __name__ == '__main__':
    a = sys.argv[1:]
    if a[0] == 'hold':
        guard(); d = DIK[a[1]]; send(d, False); time.sleep(int(a[2]) / 1000); send(d, True)
    else:
        for k in a:
            if k[0] == 'w' and k[1:].isdigit(): time.sleep(int(k[1:]) / 1000)
            else: tap(k)
