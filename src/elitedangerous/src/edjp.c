/* dinput8.dll for the Elite Dangerous Japanese patch.
   - forwards every dinput8 export to the system dinput8.dll
   Built with /DEDTEST it also contains the test harness (testio.inc, watch.inc, netlog.inc). */
#ifdef EDTEST
#include <winsock2.h>
#endif
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <tlhelp32.h>
#define DIRECTINPUT_VERSION 0x0800
#ifdef EDTEST
#include <initguid.h>
#endif
#include <dinput.h>

static HMODULE real;
static FARPROC p_create, p_canunload, p_getclass, p_reg, p_unreg, p_joy;
wchar_t g_dir[MAX_PATH];                 /* folder of this DLL (= game folder) */
uint8_t *g_exe;                          /* EliteDangerous64.exe image base */
void dlog(const char *fmt, ...);
void gfx_init(void);
void text_init(void);
void tips_init(void);
int text_set_table(const uint8_t *tab, size_t size);
static HMODULE g_self;

/* data: RCDATA resource of this DLL; the test build prefers a file of the same name next to the DLL */
const uint8_t *load_blob(const wchar_t *name, int resid, size_t *size) {
    *size = 0;
#ifdef EDTEST
    {
        wchar_t fn[MAX_PATH];
        HANDLE h;
        lstrcpyW(fn, g_dir); lstrcatW(fn, name);
        h = CreateFileW(fn, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD sz = GetFileSize(h, NULL), got = 0;
            uint8_t *b = (uint8_t *)VirtualAlloc(NULL, sz + 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (b && ReadFile(h, b, sz, &got, NULL) && got == sz) { *size = sz; CloseHandle(h); dlog("blob %ls from file, %u bytes", name, sz); return b; }
            CloseHandle(h);
        }
    }
#endif
    {
        HRSRC r = FindResourceW(g_self, MAKEINTRESOURCEW(resid), (LPCWSTR)RT_RCDATA);
        HGLOBAL g = r ? LoadResource(g_self, r) : NULL;
        if (!g) return NULL;
        *size = SizeofResource(g_self, r);
        return (const uint8_t *)LockResource(g);
    }
}

static wchar_t logpath[MAX_PATH];
static CRITICAL_SECTION g_logcs;
void dlog(const char *fmt, ...) {
    FILE *f;
    if (!logpath[0]) return;
    EnterCriticalSection(&g_logcs);
    f = _wfopen(logpath, L"a");
    if (f) {
        va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
        fputc('\n', f); fclose(f);
    }
    LeaveCriticalSection(&g_logcs);
}

static void load_real(void) {
    wchar_t p[MAX_PATH];
    if (real) return;
    GetSystemDirectoryW(p, MAX_PATH);
    lstrcatW(p, L"\\dinput8.dll");
    real = LoadLibraryW(p);
    if (!real) return;
    p_create = GetProcAddress(real, "DirectInput8Create");
    p_canunload = GetProcAddress(real, "DllCanUnloadNow");
    p_getclass = GetProcAddress(real, "DllGetClassObject");
    p_reg = GetProcAddress(real, "DllRegisterServer");
    p_unreg = GetProcAddress(real, "DllUnregisterServer");
    p_joy = GetProcAddress(real, "GetdfDIJoystick");
}

static int hook_iat(HMODULE mod, const char *dll, const char *fn, void *hook) {
    BYTE *b = (BYTE *)mod;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *id = (IMAGE_IMPORT_DESCRIPTOR *)(b + dd.VirtualAddress);
    int n = 0;
    if (!dd.VirtualAddress) return 0;
    for (; id->Name; id++) {
        IMAGE_THUNK_DATA *names, *addrs;
        if (lstrcmpiA((char *)(b + id->Name), dll)) continue;
        names = (IMAGE_THUNK_DATA *)(b + (id->OriginalFirstThunk ? id->OriginalFirstThunk : id->FirstThunk));
        addrs = (IMAGE_THUNK_DATA *)(b + id->FirstThunk);
        for (; names->u1.AddressOfData; names++, addrs++) {
            IMAGE_IMPORT_BY_NAME *ibn;
            DWORD old;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            ibn = (IMAGE_IMPORT_BY_NAME *)(b + names->u1.AddressOfData);
            if (strcmp((char *)ibn->Name, fn)) continue;
            VirtualProtect(&addrs->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
            addrs->u1.Function = (ULONG_PTR)hook;
            VirtualProtect(&addrs->u1.Function, sizeof(void *), old, &old);
            n++;
        }
    }
    dlog("iat %s!%s: %d", dll, fn, n);
    return n;
}

#ifdef EDTEST
#include "testio.inc"
#include "watch.inc"
uint8_t *find_pat(uint8_t *base, const char *pat);
void *hook_at(uint8_t *target, int stolen, void *detour);
#include "netlog.inc"

/* unique window title so the harness never depends on the foreground window */
static HWND WINAPI h_CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD st, int x, int y, int w, int h, HWND p, HMENU m, HINSTANCE i, LPVOID l) {
    HWND r = CreateWindowExW(ex, cls, name, st, x, y, w, h, p, m, i, l);
    wchar_t c[64];
    if (r && GetClassNameW(r, c, 64) && !lstrcmpW(c, L"FrontierDevelopmentsAppWinClass")) SetWindowTextW(r, L"EDJP TEST");
    return r;
}
static BOOL WINAPI h_SetWindowTextW(HWND hw, LPCWSTR t) {
    wchar_t c[64];
    if (GetClassNameW(hw, c, 64) && !lstrcmpW(c, L"FrontierDevelopmentsAppWinClass")) t = L"EDJP TEST";
    return SetWindowTextW(hw, t);
}
#endif

/* exported under their real names through dinput8.def */
HRESULT WINAPI P_DirectInput8Create(HINSTANCE h, DWORD v, REFIID r, LPVOID *o, LPUNKNOWN u) {
    HRESULT hr;
    load_real();
    if (!p_create) return E_FAIL;
    hr = ((HRESULT (WINAPI *)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN))p_create)(h, v, r, o, u);
#ifdef EDTEST
    di_patch_all(p_create);
#endif
    return hr;
}
HRESULT WINAPI P_DllCanUnloadNow(void) { load_real(); return p_canunload ? ((HRESULT (WINAPI *)(void))p_canunload)() : S_FALSE; }
HRESULT WINAPI P_DllGetClassObject(REFCLSID c, REFIID r, LPVOID *o) { load_real(); return p_getclass ? ((HRESULT (WINAPI *)(REFCLSID, REFIID, LPVOID *))p_getclass)(c, r, o) : E_FAIL; }
HRESULT WINAPI P_DllRegisterServer(void) { load_real(); return p_reg ? ((HRESULT (WINAPI *)(void))p_reg)() : E_FAIL; }
HRESULT WINAPI P_DllUnregisterServer(void) { load_real(); return p_unreg ? ((HRESULT (WINAPI *)(void))p_unreg)() : E_FAIL; }
LPCVOID WINAPI P_GetdfDIJoystick(void) { load_real(); return p_joy ? ((LPCVOID (WINAPI *)(void))p_joy)() : NULL; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID x) {
    if (r == DLL_PROCESS_ATTACH) {
        HMODULE exe = GetModuleHandleW(NULL);
        wchar_t exename[MAX_PATH];
        DWORD n;
        DisableThreadLibraryCalls(h);
        g_self = h;
        InitializeCriticalSection(&g_logcs);
        GetModuleFileNameW(exe, exename, MAX_PATH);
        if (!wcsstr(exename, L"EliteDangerous64.exe")) return TRUE;
        g_exe = (uint8_t *)exe;
        n = GetModuleFileNameW(h, g_dir, MAX_PATH);
        while (n && g_dir[n - 1] != L'\\') n--;
        g_dir[n] = 0;
#ifdef EDTEST
        {
            wchar_t ev[MAX_PATH];
            if (GetEnvironmentVariableW(L"EDJP_LOG", ev, MAX_PATH)) lstrcpyW(logpath, ev);
            else { lstrcpyW(logpath, g_dir); lstrcatW(logpath, L"edjp_log.txt"); }
        }
        testio_init(exe);
        watch_init();
        hook_iat(exe, "USER32.dll", "CreateWindowExW", h_CreateWindowExW);
        hook_iat(exe, "USER32.dll", "SetWindowTextW", h_SetWindowTextW);
        netlog_init(exe);   /* before the edjp_off switch, so the English run is recorded too */
#endif
        dlog("edjp loaded, exe base %p", g_exe);
#ifdef EDTEST
        {   /* test switch: a file "edjp_off" next to the DLL keeps only the harness (no translation hooks) */
            wchar_t fn[MAX_PATH]; lstrcpyW(fn, g_dir); lstrcatW(fn, L"edjp_off");
            if (GetFileAttributesW(fn) != INVALID_FILE_ATTRIBUTES) { dlog("all hooks off"); return TRUE; }
        }
#endif
        {
            size_t sz;
            const uint8_t *t = load_blob(L"edjp_text.bin", 101, &sz);
            if (t) text_set_table(t, sz);
        }
        text_init();
#ifdef EDTEST
        {   /* test switch: a file "edjp_notips" next to the DLL disables the loading-tips hook */
            wchar_t fn[MAX_PATH]; lstrcpyW(fn, g_dir); lstrcatW(fn, L"edjp_notips");
            if (GetFileAttributesW(fn) == INVALID_FILE_ATTRIBUTES) tips_init(); else dlog("tips hook disabled");
        }
#else
        tips_init();
#endif
        gfx_init();
    }
    return TRUE;
}
