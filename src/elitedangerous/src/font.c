/* Add our Japanese glyphs to the game's own font library at load time.
   The game's fonts_roman movie (CFX = "CFX", version, u32 length, zlib) holds DefineFont3 fonts; we
   inflate it, append our glyphs (sorted by code) to every font and hand the loader an uncompressed GFX.
   Only our glyphs (from Noto Sans JP) are shipped; the game's own font data is never copied out.

   Glyph blob ("EDJF", tools/make_glyphs.py):
     u32 magic, u32 nsets, u32 nmap
     nmap * { char name[32]; u32 set; }              font name -> glyph set (weight)
     nsets * { u32 count, codes, adv, offs, shapes } offsets from the blob start:
        u16 codes[count] (ascending), s16 adv[count], u32 offs[count + 1] (into shapes), shape records */
#include <windows.h>
#include <stdint.h>
#include <string.h>
void dlog(const char *fmt, ...);
long zinflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen);

typedef struct { uint32_t count, codes, adv, offs, shapes; } Set;
typedef struct { char name[32]; uint32_t set; } Map;
static const uint8_t *G;

static const Set *set_for(const uint8_t *name, int nl) {
    uint32_t nsets = ((const uint32_t *)G)[1], nmap = ((const uint32_t *)G)[2], i;
    const Map *m = (const Map *)(G + 12);
    const Set *s = (const Set *)(G + 12 + nmap * sizeof(Map));
    char nm[64];
    if (nl > 63) nl = 63;
    memcpy(nm, name, nl); nm[nl] = 0;
    for (i = 0; i < nmap; i++) if (!strcmp(m[i].name, nm) && m[i].set < nsets) return &s[m[i].set];
    return nmap ? &s[0] : NULL;                     /* unknown font: regular weight */
}

static uint32_t rect_len(const uint8_t *p) { uint32_t nb = p[0] >> 3; return (5 + 4 * nb + 7) / 8; }

#define W16(v) do { uint16_t t_ = (uint16_t)(v); memcpy(o, &t_, 2); o += 2; } while (0)
#define W32(v) do { uint32_t t_ = (uint32_t)(v); memcpy(o, &t_, 4); o += 4; } while (0)

/* merge one DefineFont3 body into o; returns end of output or NULL on a format surprise */
static uint8_t *merge_font(const uint8_t *d, uint32_t len, uint8_t *o) {
    uint8_t flags = d[2], nl = d[4];
    const uint8_t *name = d + 5, *p = d + 5 + nl, *tab, *codes, *lay, *adv, *rects, *e = d + len;
    uint32_t n = (uint32_t)(p[0] | p[1] << 8), i, j, k, total, *offs = NULL;
    const Set *s;
    const uint16_t *sc; const int16_t *sa; const uint32_t *so; const uint8_t *ss;
    const uint8_t **rp; uint32_t *rl;
    uint8_t *start = o, *offpos;
    int wide = flags & 0x08;
    if (!(flags & 0x04) || !(flags & 0x80)) return NULL;           /* wide codes + layout expected */
    tab = p + 2;
    offs = (uint32_t *)HeapAlloc(GetProcessHeap(), 0, (n + 1) * 4);
    rp = (const uint8_t **)HeapAlloc(GetProcessHeap(), 0, (n + 1) * sizeof(void *));
    rl = (uint32_t *)HeapAlloc(GetProcessHeap(), 0, (n + 1) * 4);
    if (!offs || !rp || !rl) return NULL;
    for (i = 0; i <= n; i++) offs[i] = wide ? (uint32_t)(tab[4 * i] | tab[4 * i + 1] << 8 | tab[4 * i + 2] << 16 | tab[4 * i + 3] << 24) : (uint32_t)(tab[2 * i] | tab[2 * i + 1] << 8);
    codes = tab + offs[n];
    lay = codes + 2 * n;                      /* asc, desc, lead */
    adv = lay + 6;
    rects = adv + 2 * n;
    for (i = 0, p = rects; i < n; i++) { rp[i] = p; rl[i] = rect_len(p); p += rl[i]; }
    /* p = kerning count, then records, then anything else: copied as is */
    if (p > e) return NULL;
    s = set_for(name, nl && name[nl - 1] == 0 ? nl - 1 : nl);
    sc = (const uint16_t *)(G + s->codes); sa = (const int16_t *)(G + s->adv);
    so = (const uint32_t *)(G + s->offs); ss = G + s->shapes;
    /* count merged glyphs */
    total = n;
    for (j = 0, i = 0; j < s->count; j++) {
        uint16_t c = sc[j];
        while (i < n && (uint16_t)(codes[2 * i] | codes[2 * i + 1] << 8) < c) i++;
        if (!(i < n && (uint16_t)(codes[2 * i] | codes[2 * i + 1] << 8) == c)) total++;
    }
    if (total > 65535) total = 65535;
    /* header */
    W16(d[0] | d[1] << 8); *o++ = flags | 0x08; *o++ = d[3]; *o++ = nl; memcpy(o, name, nl); o += nl;
    W16(total);
    offpos = o; o += 4 * (total + 1);
    /* glyph order: two-pointer merge; pass 1 shapes, pass 2 codes, adv, rects */
    {
        uint32_t base = 4 * (total + 1), cnt = 0;
        uint8_t *shp = o;
        for (i = 0, j = 0; (i < n || j < s->count) && cnt < total;) {
            uint16_t ci = i < n ? (uint16_t)(codes[2 * i] | codes[2 * i + 1] << 8) : 0xffff;
            uint16_t cj = j < s->count ? sc[j] : 0xffff;
            uint32_t at = (uint32_t)(o - shp) + base;
            memcpy(offpos + 4 * cnt, &at, 4);
            if (i < n && (ci <= cj || j >= s->count)) {
                memcpy(o, tab + offs[i], offs[i + 1] - offs[i]); o += offs[i + 1] - offs[i];
                if (ci == cj) j++;
                i++;
            } else {
                memcpy(o, ss + so[j], so[j + 1] - so[j]); o += so[j + 1] - so[j];
                j++;
            }
            cnt++;
        }
        { uint32_t at = (uint32_t)(o - shp) + base; memcpy(offpos + 4 * cnt, &at, 4); }
        for (k = 0; k < 3; k++) {             /* 0 codes, 1 advances, 2 rects */
            if (k == 1) { memcpy(o, lay, 6); o += 6; }
            for (i = 0, j = 0, cnt = 0; (i < n || j < s->count) && cnt < total; cnt++) {
                uint16_t ci = i < n ? (uint16_t)(codes[2 * i] | codes[2 * i + 1] << 8) : 0xffff;
                uint16_t cj = j < s->count ? sc[j] : 0xffff;
                if (i < n && (ci <= cj || j >= s->count)) {
                    if (k == 0) W16(ci);
                    else if (k == 1) { memcpy(o, adv + 2 * i, 2); o += 2; }
                    else { memcpy(o, rp[i], rl[i]); o += rl[i]; }
                    if (ci == cj) j++;
                    i++;
                } else {
                    if (k == 0) W16(cj);
                    else if (k == 1) W16(sa[j]);
                    else *o++ = 0;           /* empty RECT */
                    j++;
                }
            }
        }
    }
    memcpy(o, p, e - p); o += e - p;          /* kerning + rest */
    HeapFree(GetProcessHeap(), 0, offs); HeapFree(GetProcessHeap(), 0, (void *)rp); HeapFree(GetProcessHeap(), 0, rl);
    (void)start;
    return o;
}

/* game's CFX/GFX font library + our glyphs -> new uncompressed GFX (VirtualAlloc'd), NULL on failure */
uint8_t *font_build(const uint8_t *glyphs, const uint8_t *src, size_t srclen, size_t *outlen) {
    uint8_t *raw = NULL, *out = NULL, *o;
    size_t rawlen, cap, extra = 0;
    uint32_t nsets, i, nb, hdr;
    const uint8_t *q, *end;
    G = glyphs;
    if (!G || *(const uint32_t *)G != 0x464a4445 || srclen < 16) return NULL;   /* "EDJF" */
    nsets = ((const uint32_t *)G)[1];
    if (!memcmp(src, "CFX", 3)) {
        size_t guess = (size_t)*(const uint32_t *)(src + 4) + (1 << 20);
        raw = (uint8_t *)VirtualAlloc(NULL, guess, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!raw) return NULL;
        memcpy(raw, src, 8);
        {
            long r = zinflate(src + 8, srclen - 8, raw + 8, guess - 8);
            if (r < 0) { dlog("font library: inflate failed"); VirtualFree(raw, 0, MEM_RELEASE); return NULL; }
            rawlen = 8 + (size_t)r;
        }
    } else if (!memcmp(src, "GFX", 3) || !memcmp(src, "FWS", 3)) {
        raw = (uint8_t *)src; rawlen = srclen;
    } else return NULL;
    {   /* room: every font may get every glyph of the biggest set */
        const Set *s = (const Set *)(G + 12 + ((const uint32_t *)G)[2] * sizeof(Map));
        for (i = 0; i < nsets; i++) {
            size_t sz = (size_t)s[i].count * 9 + ((const uint32_t *)(G + s[i].offs))[s[i].count];
            if (sz > extra) extra = sz;
        }
    }
    cap = rawlen + 16 * extra + (1 << 20);
    out = (uint8_t *)VirtualAlloc(NULL, cap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!out) goto fail;
    nb = raw[8] >> 3;
    hdr = 8 + (5 + nb * 4 + 7) / 8 + 4;
    memcpy(out, raw, hdr);
    out[0] = 'G'; out[1] = 'F'; out[2] = 'X';
    o = out + hdr;
    q = raw + hdr; end = raw + rawlen;
    while (q + 2 <= end) {
        uint16_t cl = (uint16_t)(q[0] | q[1] << 8);
        uint32_t code = cl >> 6, ln = cl & 0x3f, h = 2;
        if (ln == 0x3f) { ln = (uint32_t)(q[2] | q[3] << 8 | q[4] << 16 | q[5] << 24); h = 6; }
        if (q + h + ln > end) break;
        if (code == 75) {
            uint8_t *t = o, *e2;
            W16((75 << 6) | 0x3f); o += 4;
            e2 = merge_font(q + h, ln, o);
            if (!e2) { dlog("font library: unexpected font layout"); goto fail; }
            { uint32_t L = (uint32_t)(e2 - o); memcpy(t + 2, &L, 4); }
            o = e2;
        } else {
            memcpy(o, q, h + ln); o += h + ln;
        }
        q += h + ln;
        if (code == 0) break;
    }
    { uint32_t L = (uint32_t)(o - out); memcpy(out + 4, &L, 4); }
    *outlen = o - out;
    if (raw != src) VirtualFree(raw, 0, MEM_RELEASE);
    return out;
fail:
    if (raw && raw != src) VirtualFree(raw, 0, MEM_RELEASE);
    if (out) VirtualFree(out, 0, MEM_RELEASE);
    return NULL;
}
