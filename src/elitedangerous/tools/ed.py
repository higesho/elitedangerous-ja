# Drive the test game.  The game window is found by its unique title "EDJP TEST" (never the foreground window);
# input goes through the test DLL's shared memory (DirectInput keyboard/mouse), screenshots via PrintWindow.
#   python tools/ed.py start          copy build/dinput8.dll into the game, press PLAY in the launcher, wait for the window
#   python tools/ed.py shot NAME [x0 y0 x1 y1]   -> capture/NAME.png
#   python tools/ed.py key K [K..]    tap keys (esc enter space tab up down left right f1.. a..z 0..9 bksp)
#   python tools/ed.py hold K MS
#   python tools/ed.py click X Y [right]   move the virtual cursor (client coords) and click
#   python tools/ed.py move X Y
#   python tools/ed.py watch ADDR [len] [w|rw]   hardware breakpoint (log)
#   python tools/ed.py unwatch
#   python tools/ed.py wait MS
#   python tools/ed.py stop           kill the game and remove the DLL
import sys, os, time, subprocess, shutil, mmap, struct
sys.path.insert(0, os.path.dirname(__file__))
from wl import wins
from cap import cap
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
GAME = os.path.abspath(os.path.join(ROOT, '..', 'Products', 'elite-dangerous-odyssey-64'))
TITLE = 'EDJP TEST'
LOG = os.path.join(GAME, 'edjp_log.txt')
DIK = {'esc': 0x01, 'enter': 0x1c, 'space': 0x39, 'tab': 0x0f, 'bksp': 0x0e, 'up': 0xc8, 'down': 0xd0, 'left': 0xcb,
       'right': 0xcd, 'lshift': 0x2a, 'lctrl': 0x1d, 'lalt': 0x38, 'pgup': 0xc9, 'pgdn': 0xd1, 'home': 0xc7, 'end': 0xcf,
       'del': 0xd3, 'minus': 0x0c, 'equals': 0x0d, 'lbr': 0x1a, 'rbr': 0x1b, 'semi': 0x27, 'comma': 0x33, 'dot': 0x34,
       'slash': 0x35, 'np0': 0x52, 'np1': 0x4f, 'np2': 0x50, 'np3': 0x51, 'np4': 0x4b, 'np5': 0x4c, 'np6': 0x4d, 'np7': 0x47,
       'np8': 0x48, 'np9': 0x49, 'npenter': 0x9c}
for i, c in enumerate('1234567890'): DIK[c] = 0x02 + i
for row, base in (('qwertyuiop', 0x10), ('asdfghjkl', 0x1e), ('zxcvbnm', 0x2c)):
    for i, c in enumerate(row): DIK[c] = base + i
for i in range(10): DIK['f%d' % (i + 1)] = 0x3b + i
DIK['f11'] = 0x57; DIK['f12'] = 0x58
C_KEYDOWN, C_KEYUP, C_MOVETO, C_BDOWN, C_BUP, C_WAIT, C_WHEEL, C_WATCH, C_UNWATCH, C_CHAR = range(1, 11)

def hwnd():
    w = [x for x in wins(title=TITLE) if x[5]]
    if not w: raise SystemExit('window "%s" not found' % TITLE)
    return w[0][0]

def shm():
    m = mmap.mmap(-1, 12 + 256 * 16 + 12 + 32, tagname='EDJP_TESTIO')
    if struct.unpack_from('<I', m, 0)[0] != 0x50444a45: raise SystemExit('test DLL not running')
    return m

def push(m, *cmds):
    for c in cmds:
        while True:
            wr, rd = struct.unpack_from('<ii', m, 4)
            if wr - rd < 250: break
            time.sleep(0.02)
        c = list(c) + [0] * (4 - len(c))
        struct.pack_into('<4i', m, 12 + (wr & 255) * 16, *c)
        struct.pack_into('<i', m, 4, wr + 1)

def settle(m, timeout=60):
    t = time.time()
    while time.time() - t < timeout:
        wr, rd = struct.unpack_from('<ii', m, 4)
        busy = struct.unpack_from('<i', m, 12 + 256 * 16 + 8)[0]
        if wr == rd and not busy: return
        time.sleep(0.03)
    print('settle timeout (is the game polling input?)')

def tap(m, k, ms=80):
    d = DIK[k] if k in DIK else int(k, 16)
    push(m, (C_KEYDOWN, d), (C_WAIT, ms), (C_KEYUP, d), (C_WAIT, 120))

def exe_base():
    import mem
    return mem.modbase(mem.open_proc())

def running():
    o = subprocess.run('tasklist /fi "imagename eq EliteDangerous64.exe" /fo csv /nh', shell=True, capture_output=True, text=True).stdout
    return 'EliteDangerous64' in o

def start():
    if running(): raise SystemExit('game already running')
    shutil.copy(os.path.join(ROOT, 'build', 'dinput8.dll'), os.path.join(GAME, 'dinput8.dll'))
    os.environ['EDJP_LOG'] = LOG
    if not [w for w in wins(title='elite launcher') if w[5]]:
        raise SystemExit('launcher not open (start it from Steam first)')
    subprocess.run(['powershell', '-ExecutionPolicy', 'Bypass', '-File', os.path.join(ROOT, 'tools', 'uia.ps1'), '-title', 'elite launcher', '-name', 'PART_BigBtn'])
    t = time.time()
    while time.time() - t < 240:
        if [w for w in wins(title=TITLE) if w[5]]:
            print('window up after %.0fs' % (time.time() - t)); return
        time.sleep(2)
    print('no window yet')

def stop():
    subprocess.run('taskkill /F /IM EliteDangerous64.exe', shell=True)
    time.sleep(3)
    try: os.remove(os.path.join(GAME, 'dinput8.dll'))
    except OSError as e: print(e)

def shot(name, box=None):
    out = os.path.join(ROOT, 'capture', name + '.png')
    r = cap(hwnd(), out)
    if box:
        from PIL import Image
        Image.open(out).crop(box).save(out)
    print(out, r)

if __name__ == '__main__':
    a = sys.argv[1:]
    cmd = a[0]
    if cmd == 'start': start()
    elif cmd == 'stop': stop()
    elif cmd == 'shot': shot(a[1], tuple(map(int, a[2:6])) if len(a) >= 6 else None)
    elif cmd == 'key':
        m = shm()
        for k in a[1:]: tap(m, k)
        settle(m)
    elif cmd == 'hold':
        m = shm(); d = DIK[a[1]]
        push(m, (C_KEYDOWN, d), (C_WAIT, int(a[2])), (C_KEYUP, d)); settle(m)
    elif cmd in ('click', 'move'):
        m = shm(); x, y = int(a[1]), int(a[2]); b = 1 if (len(a) > 3 and a[3] == 'right') else 0
        push(m, (C_MOVETO, x, y), (C_WAIT, 150))
        if cmd == 'click': push(m, (C_BDOWN, b, 1), (C_WAIT, 100), (C_BUP, b, 1), (C_WAIT, 150))
        settle(m)
    elif cmd == 'watch':
        m = shm(); ad = int(a[1], 16)
        if ad < 0x10000000: ad += exe_base()
        ln = int(a[2]) if len(a) > 2 else 8
        rw = {'w': 1, 'rw': 3, 'x': 4}[a[3]] if len(a) > 3 else 3
        push(m, (C_WATCH, struct.unpack('<i', struct.pack('<I', ad & 0xffffffff))[0], ad >> 32, ln | (rw << 8))); settle(m)
    elif cmd == 'unwatch':
        m = shm(); push(m, (C_UNWATCH,)); settle(m)
    elif cmd == 'wait':
        m = shm(); push(m, (C_WAIT, int(a[1]))); settle(m)
