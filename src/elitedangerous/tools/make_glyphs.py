# Our Japanese glyphs only (Noto Sans JP, OFL) -> build/glyphs.bin, embedded in the DLL.
# The DLL adds them to the game's own fonts_roman at load time (src/font.c); no game data is shipped.
#   python tools/make_glyphs.py build/chars.txt
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from make_font import base_chars, jp_glyphs

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
WEIGHTS = [400, 500, 600, 700]
FONTS = {'Telegrama Render': 500, 'Euro Caps': 600, 'Sintony Bold': 700, 'Sintony': 400, 'Arial': 400}

def main():
    chars = base_chars()
    chars |= set(ord(ch) for ch in open(sys.argv[1], encoding='utf-8').read() if ord(ch) >= 0x2000)
    sets = []
    for w in WEIGHTS:
        g = jp_glyphs(w, chars)
        sets.append(sorted(g.items()))
    nmap = len(FONTS)
    head = 12 + nmap * 36 + len(sets) * 20
    blob = bytearray()
    hdrs = []
    for s in sets:
        n = len(s)
        def off(): return head + len(blob)
        c_off = off(); blob += struct.pack('<%dH' % n, *[c for c, _ in s])
        while len(blob) % 4: blob += b'\0'
        a_off = off(); blob += struct.pack('<%dh' % n, *[adv for _, (sh, adv) in s])
        while len(blob) % 4: blob += b'\0'
        o_off = off()
        offs = [0]
        for _, (sh, adv) in s: offs.append(offs[-1] + len(sh))
        blob += struct.pack('<%dI' % (n + 1), *offs)
        s_off = off()
        for _, (sh, adv) in s: blob += sh
        while len(blob) % 4: blob += b'\0'
        hdrs.append(struct.pack('<5I', n, c_off, a_off, o_off, s_off))
    out = struct.pack('<3I', 0x464a4445, len(sets), nmap)
    for name, w in FONTS.items():
        out += name.encode().ljust(32, b'\0') + struct.pack('<I', WEIGHTS.index(w))
    out += b''.join(hdrs) + bytes(blob)
    open(os.path.join(ROOT, 'build', 'glyphs.bin'), 'wb').write(out)
    print('sets', [len(s) for s in sets], 'bytes', len(out))

if __name__ == '__main__':
    main()
