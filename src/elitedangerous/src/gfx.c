/* Scaleform movie loading: LoadProcess::BeginSWFLoading(this, File *in, ...) */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
void dlog(const char *fmt, ...);
uint8_t *find_pat(uint8_t *base, const char *pat);
void *hook_at(uint8_t *target, int stolen, void *detour);
extern uint8_t *g_exe;
extern wchar_t g_dir[MAX_PATH];

/* our font library: {data, size} like the game's own buffer descriptor (File + 0x18) */
static struct { uint8_t *data; uint64_t size; } g_fontbuf;

const uint8_t *load_blob(const wchar_t *name, int resid, size_t *size);
uint8_t *font_build(const uint8_t *glyphs, const uint8_t *src, size_t srclen, size_t *outlen);
static const uint8_t *g_glyphs;
static void load_font_file(void) {
    size_t sz;
    g_glyphs = load_blob(L"edjp_glyphs.bin", 102, &sz);
    dlog("glyphs %u bytes", (unsigned)(g_glyphs ? sz : 0));
}

static void *g_font_file, *g_font_origbuf;
static void *(*o_file_dtor)(void *, unsigned);
static void *h_file_dtor(void *self, unsigned flags) {
    if (self == g_font_file) {
        *(void **)((uint8_t *)self + 0x18) = g_font_origbuf;
        g_font_file = NULL;
        dlog("fonts_roman file closed");
    }
    return o_file_dtor(self, flags);
}
typedef intptr_t (*BeginFn)(void *, void *, void *, void *, void *, void *, void *, void *);
static BeginFn o_begin;

static const char *file_path(void *f) {
    void **vt = *(void ***)f;
    const char *p = NULL;
    __try { p = ((const char *(*)(void *))vt[1])(f); } __except (EXCEPTION_EXECUTE_HANDLER) { p = NULL; }
    return p;
}

static intptr_t h_begin(void *self, void *f, void *a3, void *a4, void *a5, void *a6, void *a7, void *a8) {
#ifdef EDTEST
    const char *p = f ? file_path(f) : NULL;
    uint8_t *o = (uint8_t *)f;
    dlog("SWF load %p path %s vt %llx | %016llx %016llx %016llx %016llx %016llx %016llx %016llx %016llx", f, p ? p : "?",
         (unsigned long long)(*(uint8_t **)f - g_exe),
         *(uint64_t *)(o + 8), *(uint64_t *)(o + 16), *(uint64_t *)(o + 24), *(uint64_t *)(o + 32),
         *(uint64_t *)(o + 40), *(uint64_t *)(o + 48), *(uint64_t *)(o + 56), *(uint64_t *)(o + 64));
    if (p && !strcmp(p, "fonts_roman")) {
        uint8_t **buf = *(uint8_t ***)(o + 0x18);
        wchar_t fn[MAX_PATH]; FILE *fp;
        dlog("  buf %p data %p size %llu pos %llu", buf, buf[0], (unsigned long long)(uintptr_t)buf[1], *(unsigned long long *)(o + 0x28));
        lstrcpyW(fn, g_dir); lstrcatW(fn, L"edjp_fonts_roman.bin");
        fp = _wfopen(fn, L"wb");
        if (fp) { fwrite(buf[0], 1, (size_t)(uintptr_t)buf[1], fp); fclose(fp); }
    }
#endif
    if (g_glyphs && f) {
        const char *q = file_path(f);
        if (q && !strcmp(q, "fonts_roman") && !g_font_file) {
            struct { uint8_t *data; uint64_t size; } *src = *(void **)((uint8_t *)f + 0x18);
            size_t n = 0;
            uint8_t *lib = NULL;
            __try { lib = font_build(g_glyphs, src->data, (size_t)src->size, &n); } __except (EXCEPTION_EXECUTE_HANDLER) { lib = NULL; }
            if (g_fontbuf.data) VirtualFree(g_fontbuf.data, 0, MEM_RELEASE);
            g_fontbuf.data = lib; g_fontbuf.size = lib ? n : 0;
            dlog("font library built: %u -> %u bytes", (unsigned)src->size, (unsigned)n);
        }
        if (q && !strcmp(q, "fonts_roman") && g_fontbuf.size && !g_font_file) {
            /* the File frees its buffer with the game's allocator when destroyed: point it at ours while
               loading and give the original back in the destructor (vtable slot 0) */
            void **vt = *(void ***)f;
            DWORD old;
            g_font_file = f;
            g_font_origbuf = *(void **)((uint8_t *)f + 0x18);
            if (vt[0] != (void *)h_file_dtor) {
                o_file_dtor = (void *(*)(void *, unsigned))vt[0];
                VirtualProtect(vt, 8, PAGE_READWRITE, &old); vt[0] = (void *)h_file_dtor; VirtualProtect(vt, 8, old, &old);
            }
            *(void **)((uint8_t *)f + 0x18) = &g_fontbuf;
            *(uint64_t *)((uint8_t *)f + 0x28) = 0;
            dlog("fonts_roman replaced");
        }
    }
    return o_begin(self, f, a3, a4, a5, a6, a7, a8);
}

void gfx_init(void) {
    uint8_t *p = find_pat(g_exe, "3D 47 46 58 00 74 ?? 3D 43 46 58 00 74");
    uint8_t *f;
    static const uint8_t pro[] = { 0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x6c, 0x24, 0x20, 0x56, 0x57, 0x41, 0x54 };
    if (!p) return;
    load_font_file();
    f = p - 0xd0;
    if (memcmp(f, pro, sizeof pro)) { dlog("BeginSWFLoading prologue mismatch"); return; }
    o_begin = (BeginFn)hook_at(f, 14, (void *)h_begin);
    dlog("BeginSWFLoading hooked at %llx", (unsigned long long)(f - g_exe));
}
