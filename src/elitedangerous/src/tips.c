/* Loading-screen tips come from the server (2.0/elite/resources/loadingscreentips) as JSON
   {"loadingscreentips":[{"icon":..,"desc":..,"head":..,"hint":..},...]} in English.  The same English
   strings are in our table, so the JSON is rewritten right after the HTTP body is read
   (GetBody(request, String **out)).

   Game string: {u64 length, u64 capacity, u32 refcount, char data[] at +0x14}.  Our replacement lives
   in our own memory with a huge refcount, so the game never frees it. */
#include <windows.h>
#include <stdint.h>
#include <string.h>
void dlog(const char *fmt, ...);
uint8_t *find_pat(uint8_t *base, const char *pat);
void *hook_at(uint8_t *target, int stolen, void *detour);
const uint8_t *text_lookup(const uint8_t *s, uint32_t n, uint32_t *outlen);
extern uint8_t *g_exe;

typedef void *(*BodyFn)(void *, uint8_t **);
static BodyFn o_body;

typedef struct { char *p; size_t n, cap; } Buf;
static void put(Buf *b, const void *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t c = (b->cap ? b->cap * 2 : 4096); while (c < b->n + n + 1) c *= 2;
        b->p = (char *)HeapReAlloc(GetProcessHeap(), 0, b->p, c); b->cap = c;
    }
    memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0;
}
static void putc_(Buf *b, char c) { put(b, &c, 1); }

static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }

/* JSON string body (between quotes, escapes kept) -> UTF-8 */
static void unescape(const char *s, size_t n, Buf *o) {
    size_t i;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c != '\\' || i + 1 >= n) { putc_(o, c); continue; }
        c = s[++i];
        switch (c) {
        case 'n': putc_(o, '\n'); break; case 't': putc_(o, '\t'); break; case 'r': putc_(o, '\r'); break;
        case 'b': putc_(o, '\b'); break; case 'f': putc_(o, '\f'); break;
        case 'u': {
            unsigned u = 0; int k;
            if (i + 4 >= n) break;
            for (k = 1; k <= 4; k++) u = u * 16 + hexv(s[i + k]);
            i += 4;
            if (u >= 0xd800 && u < 0xdc00 && i + 6 < n && s[i + 1] == '\\' && s[i + 2] == 'u') {
                unsigned l = 0;
                for (k = 3; k <= 6; k++) l = l * 16 + hexv(s[i + k]);
                u = 0x10000 + ((u - 0xd800) << 10) + (l - 0xdc00); i += 6;
            }
            if (u < 0x80) putc_(o, (char)u);
            else if (u < 0x800) { putc_(o, (char)(0xc0 | u >> 6)); putc_(o, (char)(0x80 | (u & 63))); }
            else if (u < 0x10000) { putc_(o, (char)(0xe0 | u >> 12)); putc_(o, (char)(0x80 | (u >> 6 & 63))); putc_(o, (char)(0x80 | (u & 63))); }
            else { putc_(o, (char)(0xf0 | u >> 18)); putc_(o, (char)(0x80 | (u >> 12 & 63))); putc_(o, (char)(0x80 | (u >> 6 & 63))); putc_(o, (char)(0x80 | (u & 63))); }
            break; }
        default: putc_(o, c); break;     /* \" \\ \/ */
        }
    }
}
static void escape(const uint8_t *s, size_t n, Buf *o) {
    size_t i;
    for (i = 0; i < n; i++) {
        uint8_t c = s[i];
        if (c == '"' || c == '\\') { putc_(o, '\\'); putc_(o, (char)c); }
        else if (c == '\n') put(o, "\\n", 2);
        else if (c == '\r') put(o, "\\r", 2);
        else if (c == '\t') put(o, "\\t", 2);
        else putc_(o, (char)c);
    }
}

static uint8_t *rewrite(const char *js, size_t n, size_t *outn, int *count) {
    static const char *keys[] = { "\"desc\":\"", "\"head\":\"", "\"hint\":\"" };
    Buf o = { 0 }, tmp = { 0 };
    size_t i = 0;
    o.p = (char *)HeapAlloc(GetProcessHeap(), 0, 4096); o.cap = 4096; o.n = 0;
    tmp.p = (char *)HeapAlloc(GetProcessHeap(), 0, 4096); tmp.cap = 4096;
    *count = 0;
    while (i < n) {
        int k, hit = -1;
        for (k = 0; k < 3; k++) if (js[i] == '"' && i + 8 <= n && !memcmp(js + i, keys[k], 8)) { hit = k; break; }
        if (hit < 0) { putc_(&o, js[i++]); continue; }
        {
            size_t s = i + 8, e = s;
            const uint8_t *j; uint32_t jl;
            while (e < n && js[e] != '"') e += (js[e] == '\\') ? 2 : 1;
            if (e >= n) { put(&o, js + i, n - i); break; }
            put(&o, keys[hit], 8);
            tmp.n = 0; tmp.p[0] = 0;
            unescape(js + s, e - s, &tmp);
            j = tmp.n ? text_lookup((const uint8_t *)tmp.p, (uint32_t)tmp.n, &jl) : NULL;
            if (j) { escape(j, jl, &o); (*count)++; }
            else put(&o, js + s, e - s);
            putc_(&o, '"');
            i = e + 1;
        }
    }
    HeapFree(GetProcessHeap(), 0, tmp.p);
    *outn = o.n;
    return (uint8_t *)o.p;
}

static void *h_body(void *req, uint8_t **out) {
    void *r = o_body(req, out);
    __try {
        uint8_t *s = out ? *out : NULL;
        static const char sig[] = "{\"loadingscreentips\"";
        if (s) {
            uint64_t len = *(uint64_t *)s;
            const char *d = (const char *)s + 0x14;
            if (len > sizeof sig && len < (16u << 20) && !memcmp(d, sig, sizeof sig - 1)) {
                size_t n; int cnt;
                uint8_t *js = rewrite(d, (size_t)len, &n, &cnt);
                uint8_t *ns = (uint8_t *)VirtualAlloc(NULL, n + 0x20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                if (ns) {
                    *(uint64_t *)ns = n; *(uint64_t *)(ns + 8) = n; *(uint32_t *)(ns + 0x10) = 0x40000000;
                    memcpy(ns + 0x14, js, n); ns[0x14 + n] = 0;
                    *out = ns;          /* the original string is left alone (one small leak per request) */
                }
                HeapFree(GetProcessHeap(), 0, js);
                dlog("loading tips: %d strings translated", cnt);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { dlog("loading tips rewrite failed"); }
    return r;
}

void tips_init(void) {
    uint8_t *p = find_pat(g_exe, "40 53 48 83 EC 20 48 8B 49 28 48 8B DA 48 85 C9 74 ?? 48 8B 01 FF 50 28 48 8B C8 48 8D 54 24 30 E8");
    if (!p) return;
    o_body = (BodyFn)hook_at(p, 16, (void *)h_body);
    dlog("HTTP GetBody hooked at %llx", (unsigned long long)(p - g_exe));
}
