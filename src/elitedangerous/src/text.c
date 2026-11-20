/* Translation: every localised string reaches the game through TxtToString(const txt *, String *out)
   (txt = [u32 length][UTF-8 bytes]).  We look the English bytes up in our table (FNV-1a 64 hash,
   sorted) and pass the Japanese txt instead.

   Table ("EDJT"): u32 magic, u32 count, count * {u64 hash, u32 offset, u32 pad}, then the strings,
   each [u32 length][UTF-8][NUL] at a 4-byte aligned offset from the table start. */
#include <windows.h>
#include <stdint.h>
#include <string.h>
void dlog(const char *fmt, ...);
uint8_t *find_pat(uint8_t *base, const char *pat);
void *hook_at(uint8_t *target, int stolen, void *detour);
extern uint8_t *g_exe;
extern wchar_t g_dir[MAX_PATH];

typedef struct { uint64_t hash; uint32_t off, pad; } Ent;
static const uint8_t *g_tab;
static const Ent *g_ent;
static uint32_t g_n;

static uint64_t fnv(const uint8_t *p, uint32_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    while (n--) { h ^= *p++; h *= 0x100000001b3ull; }
    return h;
}

static const uint32_t *lookup(const uint32_t *txt) {
    uint64_t h;
    uint32_t lo = 0, hi = g_n;
    if (!g_n || !txt || !*txt || *txt > 100000) return NULL;
    h = fnv((const uint8_t *)(txt + 1), *txt);
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (g_ent[mid].hash < h) lo = mid + 1; else hi = mid;
    }
    if (lo < g_n && g_ent[lo].hash == h) return (const uint32_t *)(g_tab + g_ent[lo].off);
    return NULL;
}

/* English UTF-8 bytes -> Japanese UTF-8 (NULL if not in the table) */
const uint8_t *text_lookup(const uint8_t *s, uint32_t n, uint32_t *outlen) {
    uint64_t h;
    uint32_t lo = 0, hi = g_n;
    if (!g_n || !n) return NULL;
    h = fnv(s, n);
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (g_ent[mid].hash < h) lo = mid + 1; else hi = mid;
    }
    if (lo < g_n && g_ent[lo].hash == h) {
        const uint32_t *t = (const uint32_t *)(g_tab + g_ent[lo].off);
        *outlen = *t;
        return (const uint8_t *)(t + 1);
    }
    return NULL;
}

typedef void *(*TxtFn)(const uint32_t *, void *);
static TxtFn o_txt;
#ifdef EDTEST
/* find the journal writer: log each distinct stack that asks for one of these strings */
static void txt_probe(const uint32_t *txt) {
    static const char *probe[] = { "Low Security", "Extraction", "Detention Centre", "None", NULL };
    static unsigned long long seen[64]; static int nseen;
    int i;
    for (i = 0; probe[i]; i++)
        if (*txt == strlen(probe[i]) && !memcmp(txt + 1, probe[i], *txt)) break;
    if (!probe[i]) return;
    {
        void *fr[24]; char line[600]; int n = RtlCaptureStackBackTrace(1, 24, fr, NULL), o = 0, k;
        unsigned long long h = 1469598103934665603ULL ^ i;
        for (k = 0; k < n; k++) h = (h ^ (unsigned long long)fr[k]) * 1099511628211ULL;
        for (k = 0; k < nseen; k++) if (seen[k] == h) return;
        if (nseen < 64) seen[nseen++] = h;
        for (k = 0; k < n && o < 560; k++) {
            unsigned long long a = (unsigned long long)fr[k], b = (unsigned long long)g_exe;
            o += wsprintfA(line + o, a >= b && a < b + 0x8000000 ? " %x" : " *", (unsigned)(a - b));
        }
        dlog("txt probe '%s' tid %lu:%s", probe[i], GetCurrentThreadId(), line);
    }
}
#endif
/* The journal (Saved Games\...\Journal.*.log, also uploaded to Frontier by PUT 2.0/elite/journal) writes each
   localisable value as "X" + "X_Localised" through one field writer (exe 0x834f40).  While that writer runs on a
   thread, TxtToString stays English, so the journal is byte-for-byte what the unpatched game writes. */
static DWORD g_tls = TLS_OUT_OF_INDEXES;
typedef unsigned long long (*JFn)(void *, void *, void *, void *);
static JFn o_jfield;
static unsigned long long h_jfield(void *a, void *b, void *c, void *d) {
    unsigned long long r;
    void *prev = TlsGetValue(g_tls);
    TlsSetValue(g_tls, (void *)1);
    r = o_jfield(a, b, c, d);
    TlsSetValue(g_tls, prev);
    return r;
}

static void *h_txt(const uint32_t *txt, void *out) {
    const uint32_t *j;
#ifdef EDTEST
    if (txt && *txt && *txt < 64) txt_probe(txt);
#endif
    if (g_tls != TLS_OUT_OF_INDEXES && TlsGetValue(g_tls)) return o_txt(txt, out);
    j = lookup(txt);
    return o_txt(j ? j : txt, out);
}

int text_set_table(const uint8_t *tab, size_t size) {
    if (size < 8 || *(const uint32_t *)tab != 0x544a4445) return 0;    /* "EDJT" */
    g_n = ((const uint32_t *)tab)[1];
    if (8 + (size_t)g_n * sizeof(Ent) > size) { g_n = 0; return 0; }
    g_ent = (const Ent *)(tab + 8);
    g_tab = tab;
    return 1;
}

void text_init(void) {
    uint8_t *p = find_pat(g_exe, "40 53 57 48 83 EC 28 8B 01 48 8B FA 85 C0 0F 85 ?? ?? ?? ?? 48 8B 02 48 85 C0 0F 84");
    if (!p) return;
    g_tls = TlsAlloc();
    {   /* journal field writer; if not found the journal is simply translated like before */
        uint8_t *jf = find_pat(g_exe, "4C 89 44 24 18 48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E1 48 81 EC B8 00 00 00 45 33 F6 48 8B F2 4C 89 32");
        if (jf && g_tls != TLS_OUT_OF_INDEXES) {
            o_jfield = (JFn)hook_at(jf, 16, (void *)h_jfield);
            dlog("journal field writer hooked at %llx", (unsigned long long)(jf - g_exe));
        }
    }
    o_txt = (TxtFn)hook_at(p, 14, (void *)h_txt);
    dlog("TxtToString hooked at %llx, %u strings", (unsigned long long)(p - g_exe), g_n);
}
