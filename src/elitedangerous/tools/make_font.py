# Build our fonts_roman: the game's own font library (work/fonts_roman.cfx, dumped by the test DLL) with Japanese
# glyphs from Noto Sans JP (OFL) added to every DefineFont3 font.  Latin glyphs stay the game's own.
#   python tools/make_font.py [charsfile]   -> build/fonts_roman.cfx
# Characters: kana and JIS X 0208 symbols, plus every character of charsfile (UTF-8 text, our translations).
import sys, os, struct, zlib
sys.path.insert(0, os.path.dirname(__file__))
import swf, df3
from swffont import ShapePen, shape_bytes, rect_bytes, tag
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
NOTO = r'C:\Windows\Fonts\NotoSansJP-VF.ttf'
EM = 20480
SCALE = 0.92            # Japanese glyphs a little smaller than the em box, next to the game's Latin
WEIGHT = {b'Telegrama Render': 500, b'Euro Caps': 600, b'Sintony Bold': 700, b'Sintony': 400, b'Arial': 400}

def base_chars():
    cs = set()
    for hi in range(0x81, 0xa0):
        for lo in list(range(0x40, 0x7f)) + list(range(0x80, 0xfd)):
            try:
                ch = bytes([hi, lo]).decode('cp932')
            except UnicodeDecodeError:
                continue
            if hi <= 0x84:
                if hi == 0x98 and lo > 0x72: continue
                cs.add(ord(ch))
    cs |= set(range(0x3000, 0x3040)) | set(range(0x3040, 0x30a0)) | set(range(0x30a0, 0x3100)) | set(range(0xff01, 0xff5f))
    return cs

_inst = {}
def noto(w):
    if w not in _inst:
        _inst[w] = instancer.instantiateVariableFont(TTFont(NOTO), {'wght': w})
    return _inst[w]

def jp_glyphs(w, chars):
    f = noto(w)
    cmap = f.getBestCmap(); gs = f.getGlyphSet(); hmtx = f['hmtx']
    s = EM / f['head'].unitsPerEm * SCALE
    dy = int(EM * (1 - SCALE) * 0.35)            # sink a little: centre the smaller box on the old em box
    out = {}
    for c in sorted(chars):
        if c not in cmap: continue
        gn = cmap[c]
        pen = ShapePen(gs, s); gs[gn].draw(pen)
        cont = [[(e[0],) + tuple(v + (dy if k % 2 else 0) for k, v in enumerate(e[1:])) for e in cc] for cc in pen.contours]
        adv = min(32767, int(round(hmtx[gn][0] * EM / f['head'].unitsPerEm)))   # full em advance
        # centre the scaled glyph in the full advance
        dx = int(round((adv - hmtx[gn][0] * s) / 2))
        cont = [[(e[0],) + tuple(v + (dx if k % 2 == 0 else 0) for k, v in enumerate(e[1:])) for e in cc] for cc in cont]
        out[c] = (shape_bytes(cont), adv)
    return out

def main():
    chars = base_chars()
    if len(sys.argv) > 1:
        chars |= set(ord(ch) for ch in open(sys.argv[1], encoding='utf-8').read() if ord(ch) >= 0x2000)
    src = open(os.path.join(ROOT, 'work', 'fonts_roman.cfx'), 'rb').read()
    b = src[:8] + zlib.decompress(src[8:])
    out = bytearray()
    added = {}
    cache = {}
    nb = b[8] >> 3
    q = 8 + (5 + nb * 4 + 7) // 8 + 4
    first = q
    tags = []
    while q < len(b):
        cl, = struct.unpack_from('<H', b, q); start = q; q += 2
        code, ln = cl >> 6, cl & 0x3f
        if ln == 0x3f:
            ln, = struct.unpack_from('<I', b, q); q += 4
        tags.append((code, start, q, ln)); q += ln
        if code == 0: break
    for code, start, p, ln in tags:
        if code == 75:
            f = df3.parse(b[p:p + ln])
            nm = f['name'].rstrip(b'\0')
            w = WEIGHT.get(nm, 400)
            if w not in cache: cache[w] = jp_glyphs(w, chars)
            have = set(f['codes'])
            n0 = len(f['codes'])
            for c, (sh, adv) in cache[w].items():
                if c in have: continue
                f['codes'].append(c); f['shapes'].append(sh); f['adv'].append(adv); f['bounds'].append(rect_bytes(0, 0, 0, 0))
            added[nm] = len(f['codes']) - n0
            out += tag(75, df3.build(f))
        else:
            out += b[start:p + ln]
    body = b[8:first] + bytes(out)
    gfx = b'GFX' + b[3:4] + struct.pack('<I', 8 + len(body)) + body
    cfx = b'CFX' + gfx[3:8] + zlib.compress(gfx[8:], 9)
    os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True)
    open(os.path.join(ROOT, 'build', 'fonts_roman.cfx'), 'wb').write(cfx)
    print('added', added, 'size', len(gfx), 'cfx', len(cfx))

if __name__ == '__main__':
    main()
