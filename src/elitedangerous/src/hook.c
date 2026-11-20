/* small x64 inline hook + pattern search helpers */
#include <windows.h>
#include <stdint.h>
#include <string.h>
void dlog(const char *fmt, ...);

/* pattern: hex bytes separated by spaces, "??" = any byte */
static int parse_pat(const char *p, uint8_t *b, uint8_t *m) {
    int n = 0;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (p[0] == '?') { b[n] = 0; m[n] = 0; p += (p[1] == '?') ? 2 : 1; }
        else { char h[3] = { p[0], p[1], 0 }; b[n] = (uint8_t)strtoul(h, NULL, 16); m[n] = 1; p += 2; }
        n++;
    }
    return n;
}

/* finds the unique match of pat inside the exe's .text; returns NULL if none or several */
uint8_t *find_pat(uint8_t *base, const char *pat) {
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    uint8_t b[128], m[128];
    int n = parse_pat(pat, b, m), i, k;
    uint8_t *hit = NULL;
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, s++) {
        uint8_t *p, *e;
        if (!(s->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        p = base + s->VirtualAddress; e = p + s->Misc.VirtualSize - n;
        for (; p < e; p++) {
            if (p[0] != b[0]) continue;
            for (k = 1; k < n; k++) if (m[k] && p[k] != b[k]) break;
            if (k == n) {
                if (hit) { dlog("pattern not unique: %s", pat); return NULL; }
                hit = p;
            }
        }
    }
    if (!hit) dlog("pattern not found: %s", pat);
    return hit;
}

/* overwrite target with "jmp [rip+0]; dq detour"; stolen (>= 14) bytes must be whole, position-independent
   instructions; returns a trampoline that runs them and continues at target + stolen */
void *hook_at(uint8_t *target, int stolen, void *detour) {
    uint8_t *tr;
    DWORD old;
    int i;
    if (!target || stolen < 14) return NULL;
    tr = (uint8_t *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tr) return NULL;
    memcpy(tr, target, stolen);
    tr[stolen] = 0xff; tr[stolen + 1] = 0x25; *(DWORD *)(tr + stolen + 2) = 0;
    *(void **)(tr + stolen + 6) = target + stolen;
    VirtualProtect(target, stolen, PAGE_EXECUTE_READWRITE, &old);
    target[0] = 0xff; target[1] = 0x25; *(DWORD *)(target + 2) = 0; *(void **)(target + 6) = detour;
    for (i = 14; i < stolen; i++) target[i] = 0x90;
    VirtualProtect(target, stolen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, stolen);
    return tr;
}
