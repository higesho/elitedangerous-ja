/*
 * Altered version based on zlib contrib/puff by Mark Adler (zlib license).
 * Original: Copyright (C) 2002-2013 Mark Adler
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from the use of this software.
 * Permission is granted to anyone to use this software for any purpose, including commercial
 * applications, and to alter it and redistribute it freely, subject to the following restrictions:
 * 1. The origin of this software must not be misrepresented; you must not claim that you wrote
 *    the original software.
 * 2. Altered source versions must be plainly marked as such, and must not be misrepresented as
 *    being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */
/* minimal raw-deflate decoder (RFC 1951), enough for zlib streams inside CFX movies */
#include <stdint.h>
#include <string.h>

typedef struct { const uint8_t *in; size_t inlen, pos; uint32_t bitbuf; int bitcnt; uint8_t *out; size_t outlen, outpos; } St;
typedef struct { uint16_t count[16], sym[288]; } Huff;

static int bits(St *s, int n) {
    uint32_t v = s->bitbuf;
    while (s->bitcnt < n) {
        if (s->pos >= s->inlen) return -1;
        v |= (uint32_t)s->in[s->pos++] << s->bitcnt; s->bitcnt += 8;
    }
    s->bitbuf = v >> n; s->bitcnt -= n;
    return (int)(v & ((1u << n) - 1));
}

static int build(Huff *h, const uint8_t *len, int n) {
    uint16_t offs[16];
    int i;
    memset(h->count, 0, sizeof h->count);
    for (i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0;
    offs[1] = 0;
    for (i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->count[i];
    for (i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = (uint16_t)i;
    return 0;
}

static int decode(St *s, const Huff *h) {
    int code = 0, first = 0, index = 0, len;
    for (len = 1; len < 16; len++) {
        int b = bits(s, 1);
        if (b < 0) return -1;
        code |= b;
        if (code - h->count[len] < first) return h->sym[index + (code - first)];
        index += h->count[len]; first += h->count[len]; first <<= 1; code <<= 1;
    }
    return -1;
}

static const uint16_t lbase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const uint8_t lext[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const uint16_t dbase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const uint8_t dext[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

static int codes(St *s, const Huff *lc, const Huff *dc) {
    for (;;) {
        int sym = decode(s, lc);
        if (sym < 0) return -1;
        if (sym < 256) { if (s->outpos >= s->outlen) return -2; s->out[s->outpos++] = (uint8_t)sym; }
        else if (sym == 256) return 0;
        else {
            int l, d, e;
            sym -= 257; if (sym >= 29) return -1;
            e = bits(s, lext[sym]); if (e < 0) return -1; l = lbase[sym] + e;
            sym = decode(s, dc); if (sym < 0 || sym >= 30) return -1;
            e = bits(s, dext[sym]); if (e < 0) return -1; d = dbase[sym] + e;
            if ((size_t)d > s->outpos || s->outpos + l > s->outlen) return -2;
            while (l--) { s->out[s->outpos] = s->out[s->outpos - d]; s->outpos++; }
        }
    }
}

static int dynamic(St *s) {
    static const uint8_t ord[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
    uint8_t len[320];
    Huff lc, dc;
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4, i;
    if (nlen > 286 || ndist > 30) return -1;
    memset(len, 0, sizeof len);
    for (i = 0; i < ncode; i++) len[ord[i]] = (uint8_t)bits(s, 3);
    build(&lc, len, 19);
    for (i = 0; i < nlen + ndist;) {
        int sym = decode(s, &lc), rep, v = 0;
        if (sym < 0) return -1;
        if (sym < 16) { len[i++] = (uint8_t)sym; continue; }
        if (sym == 16) { if (!i) return -1; v = len[i - 1]; rep = 3 + bits(s, 2); }
        else if (sym == 17) rep = 3 + bits(s, 3);
        else rep = 11 + bits(s, 7);
        if (i + rep > nlen + ndist) return -1;
        while (rep--) len[i++] = (uint8_t)v;
    }
    build(&lc, len, nlen);
    build(&dc, len + nlen, ndist);
    return codes(s, &lc, &dc);
}

static int fixed(St *s) {
    uint8_t len[320];
    Huff lc, dc;
    int i;
    for (i = 0; i < 144; i++) len[i] = 8;
    for (; i < 256; i++) len[i] = 9;
    for (; i < 280; i++) len[i] = 7;
    for (; i < 288; i++) len[i] = 8;
    build(&lc, len, 288);
    for (i = 0; i < 30; i++) len[i] = 5;
    build(&dc, len, 30);
    return codes(s, &lc, &dc);
}

/* zlib stream (2-byte header) -> out; returns bytes written or -1 */
long zinflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen) {
    St s;
    int last, r;
    if (inlen < 2 || (in[0] & 0x0f) != 8) return -1;
    memset(&s, 0, sizeof s);
    s.in = in + 2; s.inlen = inlen - 2; s.out = out; s.outlen = outlen;
    do {
        int type;
        last = bits(&s, 1); type = bits(&s, 2);
        if (last < 0 || type < 0) return -1;
        if (type == 0) {
            uint16_t n;
            s.bitbuf = 0; s.bitcnt = 0;
            if (s.pos + 4 > s.inlen) return -1;
            n = (uint16_t)(s.in[s.pos] | s.in[s.pos + 1] << 8); s.pos += 4;
            if (s.pos + n > s.inlen || s.outpos + n > s.outlen) return -1;
            memcpy(s.out + s.outpos, s.in + s.pos, n); s.pos += n; s.outpos += n;
            r = 0;
        } else if (type == 1) r = fixed(&s);
        else if (type == 2) r = dynamic(&s);
        else return -1;
        if (r) return -1;
    } while (!last);
    return (long)s.outpos;
}
