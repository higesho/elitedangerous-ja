# Build build/text.bin (the DLL's translation table) from an English -> Japanese dict.
#   python tools/make_text.py [ja.json ...]     (default: text/ja/*.json)  -> build/text.bin, build/chars.txt
# Each json is {english: japanese}.  Entries whose Japanese is empty or equal to the English are skipped.
import sys, os, json, struct, glob
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))

def fnv(b):
    h = 0xcbf29ce484222325
    for c in b:
        h ^= c; h = (h * 0x100000001b3) & 0xffffffffffffffff
    return h

def build(d):
    items = []
    for en, ja in d.items():
        if not ja or ja == en: continue
        items.append((fnv(en.encode('utf-8')), ja.encode('utf-8')))
    items.sort()
    hs = [h for h, _ in items]
    assert len(set(hs)) == len(hs), 'hash collision'
    n = len(items)
    head = 8 + n * 16
    blob = bytearray()
    ents = []
    for h, jb in items:
        off = head + len(blob)
        ents.append(struct.pack('<QII', h, off, 0))
        blob += struct.pack('<I', len(jb)) + jb + b'\0'
        while len(blob) % 4: blob += b'\0'
    return struct.pack('<II', 0x544a4445, n) + b''.join(ents) + bytes(blob), n

def main():
    files = sys.argv[1:] or sorted(glob.glob(os.path.join(ROOT, 'text', 'ja', '*.json')))
    d = {}
    for f in files: d.update(json.load(open(f, encoding='utf-8')))
    data, n = build(d)
    os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True)
    open(os.path.join(ROOT, 'build', 'text.bin'), 'wb').write(data)
    chars = sorted(set(ch for v in d.values() if v for ch in v if ord(ch) >= 0x2000))
    open(os.path.join(ROOT, 'build', 'chars.txt'), 'w', encoding='utf-8').write(''.join(chars))
    print('strings', n, 'bytes', len(data), 'chars', len(chars))

if __name__ == '__main__':
    main()
