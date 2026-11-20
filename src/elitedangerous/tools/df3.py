# DefineFont3 (tag 75) parse / rebuild helpers
import struct

def read_rect(b, p):
    nb = b[p] >> 3
    total = 5 + 4 * nb
    nbytes = (total + 7) // 8
    bits = int.from_bytes(b[p:p + nbytes], 'big')
    vals = []
    for i in range(4):
        shift = nbytes * 8 - 5 - nb * (i + 1)
        v = (bits >> shift) & ((1 << nb) - 1)
        if nb and v & (1 << (nb - 1)): v -= 1 << nb
        vals.append(v)
    return b[p:p + nbytes], vals, p + nbytes

def parse(d):
    """d = tag body; returns dict"""
    fid, flags, lang, nl = struct.unpack_from('<HBBB', d, 0)
    q = 5
    name = d[q:q + nl]; q += nl
    n, = struct.unpack_from('<H', d, q); q += 2
    wide_off = flags & 0x08; wide_codes = flags & 0x04; layout = flags & 0x80
    assert wide_codes, 'DefineFont3 always has wide codes'
    tab = q
    osz = 4 if wide_off else 2
    offs = list(struct.unpack_from('<%d%s' % (n + 1, 'I' if wide_off else 'H'), d, tab))
    shapes = [d[tab + offs[i]:tab + offs[i + 1]] for i in range(n)]
    p = tab + offs[n]
    codes = list(struct.unpack_from('<%dH' % n, d, p)); p += 2 * n
    f = dict(id=fid, flags=flags, lang=lang, name=name, codes=codes, shapes=shapes)
    if layout:
        asc, desc, lead = struct.unpack_from('<HHh', d, p); p += 6
        adv = list(struct.unpack_from('<%dh' % n, d, p)); p += 2 * n
        bounds = []
        for i in range(n):
            raw, vals, p = read_rect(d, p)
            bounds.append(raw)
        nk, = struct.unpack_from('<H', d, p); p += 2
        kern = [struct.unpack_from('<HHh', d, p + 6 * i) for i in range(nk)]
        p += 6 * nk
        f.update(asc=asc, desc=desc, lead=lead, adv=adv, bounds=bounds, kern=kern, tail=d[p:])
    return f

def build(f):
    n = len(f['codes'])
    order = sorted(range(n), key=lambda i: f['codes'][i])
    codes = [f['codes'][i] for i in order]
    shapes = [f['shapes'][i] for i in order]
    flags = f['flags'] | 0x08 | 0x04
    d = bytearray(struct.pack('<HBBB', f['id'], flags, f['lang'], len(f['name'])) + f['name'] + struct.pack('<H', n))
    off = 4 * n + 4
    offsets = []
    for sh in shapes: offsets.append(off); off += len(sh)
    d += struct.pack('<%dI' % n, *offsets) + struct.pack('<I', off)
    for sh in shapes: d += sh
    d += struct.pack('<%dH' % n, *codes)
    if flags & 0x80:
        d += struct.pack('<HHh', f['asc'], f['desc'], f['lead'])
        d += struct.pack('<%dh' % n, *[f['adv'][i] for i in order])
        for i in order: d += f['bounds'][i]
        d += struct.pack('<H', len(f['kern']))
        for k in f['kern']: d += struct.pack('<HHh', *k)
        d += f.get('tail', b'')
    return bytes(d)
