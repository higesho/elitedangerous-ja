/* TEST ONLY (compiled with /DEDTEST, never shipped): background input injection for tools/win.py.
   - cursor position comes from the shared memory "EDJP_TESTIO" (GetCursorPos/GetCursorInfo hooked)
   - the game always believes it is the foreground window, and may not move/clip the real cursor
     or bring itself to the front
   - DirectInput keyboard/mouse report only injected input (background, non-exclusive) */
/* ---------- shared memory ---------- */
typedef struct { LONG type, a, b, c; } Cmd;
typedef struct {
    volatile LONG magic;
    volatile LONG wr, rd;
    Cmd cmd[256];
    volatile LONG x, y;          /* cursor, client pixels */
    volatile LONG busy;
    volatile LONG counters[8];
} Shm;
enum { C_KEYDOWN = 1, C_KEYUP, C_MOVETO, C_BDOWN, C_BUP, C_WAIT, C_WHEEL, C_WATCH, C_UNWATCH, C_CHAR };
static void watch_set(ULONG64 addr, int len, int rw);
static Shm *g_shm;
static CRITICAL_SECTION g_cs;
static HWND g_hwnd;

static HWND game_hwnd(void) {
    if (!g_hwnd || !IsWindow(g_hwnd)) {
        HWND h = NULL;
        DWORD pid;
        while ((h = FindWindowExW(NULL, h, L"FrontierDevelopmentsAppWinClass", NULL))) {
            GetWindowThreadProcessId(h, &pid);
            if (pid == GetCurrentProcessId()) { g_hwnd = h; break; }
        }
    }
    return g_hwnd;
}

static HANDLE g_kev, g_mev;
static uint8_t g_keys[256];
static DIDEVICEOBJECTDATA g_kq[256]; static int g_kqn;
static LONG g_mx, g_my, g_mz; static uint8_t g_mb[8];
static DIDEVICEOBJECTDATA g_mq[256]; static int g_mqn;
static DWORD g_wait_until, g_seq;

static void q_push(DIDEVICEOBJECTDATA *q, int *n, DWORD ofs, DWORD data) {
    if (*n >= 256) return;
    q[*n].dwOfs = ofs; q[*n].dwData = data; q[*n].dwTimeStamp = GetTickCount(); q[*n].dwSequence = ++g_seq; q[*n].uAppData = 0;
    (*n)++;
}

static void post_mouse(UINT msg, WPARAM wp) {
    HWND h = game_hwnd();
    if (h) PostMessageW(h, msg, wp, MAKELPARAM(g_shm->x, g_shm->y));
}

static void vt_patch(void *dev);
/* The game's keyboard/mouse devices are wrapped by the Steam overlay (its own vtables, calling saved
   dinput8 pointers), so patch the vtables of the devices the game actually holds:
   input manager = *(exe + 0x5f2a968), devices at +(0x5b+i)*16, DI device at +0x18 (game build 4.4.2.1, test only). */
static void game_dev_patch(void) {
    static int done; static DWORD last;
    uint8_t *im; int i, n;
    if (done || GetTickCount() - last < 1000) return;
    last = GetTickCount();
    __try {
        im = *(uint8_t **)(g_exe + 0x5f2a968);
        if (!im) return;
        n = *(int *)(im + 0x8d8);
        if (n < 2 || n > 8) return;
        for (i = 0; i < n; i++) {
            uint8_t *dev = *(uint8_t **)(im + (0x5b + i) * 16);
            void *di = dev ? *(void **)(dev + 0x18) : NULL;
            if (di) vt_patch(di);
        }
        done = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { done = 1; dlog("game_dev_patch failed"); }
}
static void pump(void) {
    if (!g_shm) return;
    game_dev_patch();
    EnterCriticalSection(&g_cs);
    while ((int)(GetTickCount() - g_wait_until) >= 0 && g_shm->rd != g_shm->wr) {
        Cmd c = g_shm->cmd[g_shm->rd & 255];
        switch (c.type) {
        case C_KEYDOWN: g_keys[c.a & 255] = 0x80; q_push(g_kq, &g_kqn, c.a & 255, 0x80); break;
        case C_KEYUP:   g_keys[c.a & 255] = 0; q_push(g_kq, &g_kqn, c.a & 255, 0); break;
        case C_MOVETO: {
            LONG dx = c.a - g_shm->x, dy = c.b - g_shm->y;
            g_shm->x = c.a; g_shm->y = c.b;
            g_mx += dx; g_my += dy;
            if (dx) q_push(g_mq, &g_mqn, DIMOFS_X, (DWORD)dx);
            if (dy) q_push(g_mq, &g_mqn, DIMOFS_Y, (DWORD)dy);
            post_mouse(WM_MOUSEMOVE, 0);
            break; }
        case C_WHEEL: g_mz += c.a; q_push(g_mq, &g_mqn, DIMOFS_Z, (DWORD)c.a); break;
        case C_BDOWN:
            g_mb[c.a & 7] = 0x80; q_push(g_mq, &g_mqn, DIMOFS_BUTTON0 + (c.a & 7), 0x80);
            if (c.b) post_mouse(c.a ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, c.a ? MK_RBUTTON : MK_LBUTTON);
            break;
        case C_BUP:
            g_mb[c.a & 7] = 0; q_push(g_mq, &g_mqn, DIMOFS_BUTTON0 + (c.a & 7), 0);
            if (c.b) post_mouse(c.a ? WM_RBUTTONUP : WM_LBUTTONUP, 0);
            break;
        case C_WAIT: g_wait_until = GetTickCount() + c.a; break;
        case C_WATCH: watch_set(((ULONG64)(DWORD)c.b << 32) | (DWORD)c.a, c.c & 0xff, (c.c >> 8) & 7); break;
        case C_UNWATCH: watch_set(0, 0, 0); break;
        case C_CHAR: { HWND h = game_hwnd(); if (h) PostMessageW(h, WM_CHAR, c.a, 1); break; }
        }
        InterlockedIncrement(&g_shm->rd);
    }
    if (g_kqn && g_kev) SetEvent(g_kev);
    if (g_mqn && g_mev) SetEvent(g_mev);
    g_shm->busy = ((int)(GetTickCount() - g_wait_until) < 0 || g_shm->rd != g_shm->wr);
    LeaveCriticalSection(&g_cs);
}

/* ---------- user32 hooks ---------- */
static BOOL WINAPI h_GetCursorPos(LPPOINT p) {
    HWND h = game_hwnd();
    g_shm->counters[0]++;
    pump();
    if (!p) return FALSE;
    p->x = g_shm->x; p->y = g_shm->y;
    if (h) ClientToScreen(h, p);
    return TRUE;
}
static BOOL WINAPI h_GetCursorInfo(PCURSORINFO ci) {
    BOOL r = GetCursorInfo(ci);
    if (ci) h_GetCursorPos(&ci->ptScreenPos);
    return r;
}
static HWND WINAPI h_WindowFromPoint(POINT p) { return game_hwnd(); }
static HWND WINAPI h_GetForegroundWindow(void) { g_shm->counters[1]++; return game_hwnd(); }
static HWND WINAPI h_GetActiveWindow(void) { return game_hwnd(); }
static HWND WINAPI h_GetFocus(void) { return game_hwnd(); }
static BOOL WINAPI h_SetCursorPos(int x, int y) { return TRUE; }
static BOOL WINAPI h_ClipCursor(const RECT *r) { return TRUE; }
static BOOL WINAPI h_SetForegroundWindow(HWND h) { return TRUE; }
static HWND WINAPI h_SetActiveWindow(HWND h) { return game_hwnd(); }
static HWND WINAPI h_SetFocus(HWND h) { return game_hwnd(); }
static SHORT WINAPI h_GetKeyState(int vk) {
    static int seen[256];
    if (!seen[vk & 255]++) dlog("GetKeyState %02x", vk & 255);
    pump();
    if (vk == VK_LBUTTON) return g_mb[0] ? (SHORT)0xff80 : 0;
    if (vk == VK_RBUTTON) return g_mb[1] ? (SHORT)0xff80 : 0;
    { UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC); if (sc && sc < 256 && g_keys[sc]) return (SHORT)0xff80; }
    return 0;
}

static SHORT WINAPI h_GetAsyncKeyState(int vk) {
    static int seen[256];
    if (!seen[vk & 255]++) dlog("GetAsyncKeyState %02x", vk & 255);
    return h_GetKeyState(vk);
}
static BOOL WINAPI h_PeekMessageA(LPMSG m, HWND w, UINT a, UINT b, UINT r) {
    static unsigned char seen[0x10000];
    BOOL ok = PeekMessageA(m, w, a, b, r);
    if (ok && m->message < 0x10000 && !seen[m->message]) { seen[m->message] = 1; dlog("PeekMessageA msg %04x hwnd %p wp %llx lp %llx (filter %p %x %x %x)", m->message, m->hwnd, (ULONG64)m->wParam, (ULONG64)m->lParam, w, a, b, r); }
    return ok;
}

/* ---------- DirectInput ----------
   The game creates its keyboard/mouse devices through an interface we never see, so the hooks go
   into the class vtables of dinput8 itself (A and W, keyboard and mouse): every keyboard/mouse
   device then reports only injected input. */
typedef struct { void **vt; void *o[32]; } VTab;
static VTab g_vts[8]; static int g_nvts;
static void *g_devp[64]; static int g_devt[64]; static int g_ndev;
static void *orig(void *d, int i) {
    void **vt = *(void ***)d; int k;
    for (k = 0; k < g_nvts; k++) if (g_vts[k].vt == vt) return g_vts[k].o[i];
    return vt[i];
}
#define ORIG(d, i, T) ((T)orig(d, i))
static int devtype(void *d) {                     /* 1 keyboard, 2 mouse, 0 other */
    int k, t;
    DIDEVCAPS caps;
    for (k = 0; k < g_ndev; k++) if (g_devp[k] == d) return g_devt[k];
    memset(&caps, 0, sizeof caps); caps.dwSize = sizeof caps;
    ((HRESULT (STDMETHODCALLTYPE *)(void *, DIDEVCAPS *))(*(void ***)d)[3])(d, &caps);
    t = (caps.dwDevType & 0xff) == DI8DEVTYPE_KEYBOARD ? 1 : (caps.dwDevType & 0xff) == DI8DEVTYPE_MOUSE ? 2 : 0;
    if (g_ndev < 64) { g_devp[g_ndev] = d; g_devt[g_ndev] = t; g_ndev++; }
    dlog("DI device %p type %d (devtype %lx)", d, t, caps.dwDevType);
    return t;
}
static int ours(void *d) { return d && devtype(d) != 0; }

static HRESULT STDMETHODCALLTYPE h_SetEvent(IDirectInputDevice8W *d, HANDLE e) {
    int t = devtype(d);
    if (t == 1) g_kev = e; else if (t == 2) g_mev = e;
    dlog("DI SetEventNotification %p %p", d, e);
    return ORIG(d, 12, HRESULT (STDMETHODCALLTYPE *)(void *, HANDLE))(d, e);
}
static HRESULT STDMETHODCALLTYPE h_Acquire(IDirectInputDevice8W *d) {
    if (ours(d)) return DI_OK;
    return ORIG(d, 7, HRESULT (STDMETHODCALLTYPE *)(void *))(d);
}
static HRESULT STDMETHODCALLTYPE h_SetCoop(IDirectInputDevice8W *d, HWND w, DWORD fl) {
    if (ours(d)) fl = DISCL_BACKGROUND | DISCL_NONEXCLUSIVE;
    return ORIG(d, 13, HRESULT (STDMETHODCALLTYPE *)(void *, HWND, DWORD))(d, w, fl);
}
static HRESULT STDMETHODCALLTYPE h_GetDeviceState(IDirectInputDevice8W *d, DWORD n, LPVOID out) {
    int t = devtype(d);
    if (!t) return ORIG(d, 9, HRESULT (STDMETHODCALLTYPE *)(void *, DWORD, LPVOID))(d, n, out);
    pump();
    EnterCriticalSection(&g_cs);
    memset(out, 0, n);
    if (t == 1) { g_shm->counters[2]++; memcpy(out, g_keys, n < 256 ? n : 256); }
    else {
        DIMOUSESTATE2 st = { 0 };
        g_shm->counters[3]++;
        st.lX = g_mx; st.lY = g_my; st.lZ = g_mz; memcpy(st.rgbButtons, g_mb, 8);
        g_mx = g_my = g_mz = 0;
        memcpy(out, &st, n < sizeof st ? n : sizeof st);
    }
    LeaveCriticalSection(&g_cs);
    return DI_OK;
}
static HRESULT STDMETHODCALLTYPE h_GetDeviceData(IDirectInputDevice8W *d, DWORD sz, LPDIDEVICEOBJECTDATA out, LPDWORD inout, DWORD fl) {
    DIDEVICEOBJECTDATA *q; int *qn; DWORD max, n = 0;
    int t = devtype(d);
    if (!t) return ORIG(d, 10, HRESULT (STDMETHODCALLTYPE *)(void *, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD))(d, sz, out, inout, fl);
    pump();
    EnterCriticalSection(&g_cs);
    if (t == 1) { q = g_kq; qn = &g_kqn; g_shm->counters[4]++; } else { q = g_mq; qn = &g_mqn; g_shm->counters[5]++; }
    max = *inout;
    while (n < max && n < (DWORD)*qn) {
        if (out) memcpy((uint8_t *)out + n * sz, &q[n], sz < sizeof(DIDEVICEOBJECTDATA) ? sz : sizeof(DIDEVICEOBJECTDATA));
        n++;
    }
    if (!(fl & DIGDD_PEEK)) { memmove(q, q + n, (*qn - n) * sizeof q[0]); *qn -= n; }
    LeaveCriticalSection(&g_cs);
    *inout = n;
    return DI_OK;
}
static HRESULT STDMETHODCALLTYPE h_Poll(IDirectInputDevice8W *d) {
    if (ours(d)) return DI_OK;
    return ORIG(d, 25, HRESULT (STDMETHODCALLTYPE *)(void *))(d);
}
static void vt_patch(void *dev) {
    static const int slots[] = { 7, 9, 10, 12, 13, 25 };
    static void *hooks[] = { (void *)h_Acquire, (void *)h_GetDeviceState, (void *)h_GetDeviceData, (void *)h_SetEvent, (void *)h_SetCoop, (void *)h_Poll };
    void **vt = *(void ***)dev;
    int k; DWORD p;
    for (k = 0; k < g_nvts; k++) if (g_vts[k].vt == vt) return;
    if (g_nvts >= 8) return;
    g_vts[g_nvts].vt = vt;
    VirtualProtect(vt, 32 * sizeof(void *), PAGE_EXECUTE_READWRITE, &p);
    for (k = 0; k < 6; k++) { g_vts[g_nvts].o[slots[k]] = vt[slots[k]]; }
    g_nvts++;
    for (k = 0; k < 6; k++) vt[slots[k]] = hooks[k];
    VirtualProtect(vt, 32 * sizeof(void *), p, &p);
    dlog("DI vtable %p patched", vt);
}
static void vtp(void **vt, int i, void *r, void **o) { (void)vt; (void)i; (void)r; (void)o; }
static HRESULT (STDMETHODCALLTYPE *o_CreateDevice)(IDirectInput8W *, REFGUID, LPDIRECTINPUTDEVICE8W *, LPUNKNOWN);
static HRESULT STDMETHODCALLTYPE h_CreateDevice(IDirectInput8W *di, REFGUID g, LPDIRECTINPUTDEVICE8W *out, LPUNKNOWN u) { return o_CreateDevice(di, g, out, u); }

/* create throw-away A and W keyboard/mouse devices once, to reach the class vtables */
static void di_patch_all(FARPROC create) {
    static int done;
    const IID *iids[2] = { &IID_IDirectInput8W, &IID_IDirectInput8A };
    const GUID *devs[2] = { &GUID_SysKeyboard, &GUID_SysMouse };
    int i, j;
    if (done || !create) return;
    done = 1;
    for (i = 0; i < 2; i++) {
        IDirectInput8W *di = NULL;
        HRESULT hr = ((HRESULT (WINAPI *)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN))create)(GetModuleHandleW(NULL), 0x0800, iids[i], (void **)&di, NULL);
        dlog("DI create %d hr %lx %p", i, hr, di);
        if (FAILED(hr) || !di) continue;
        for (j = 0; j < 2; j++) {
            IDirectInputDevice8W *d = NULL;
            hr = di->lpVtbl->CreateDevice(di, devs[j], &d, NULL);
            dlog("  dev %d hr %lx %p vt %p", j, hr, d, d ? *(void **)d : NULL);
            if (SUCCEEDED(hr) && d) vt_patch(d);   /* kept alive on purpose */
        }
    }
}

static void testio_init(HMODULE exe) {
    HANDLE m;
    InitializeCriticalSection(&g_cs);
    m = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(Shm), "EDJP_TESTIO");
    g_shm = m ? (Shm *)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shm)) : NULL;
    if (!g_shm) { static Shm dummy; g_shm = &dummy; }
    if (g_shm->magic != 0x50444a45) { memset((void *)g_shm, 0, sizeof(Shm)); g_shm->magic = 0x50444a45; g_shm->x = 720; g_shm->y = 450; }
    hook_iat(exe, "USER32.dll", "GetCursorPos", h_GetCursorPos);
    hook_iat(exe, "USER32.dll", "GetCursorInfo", h_GetCursorInfo);
    hook_iat(exe, "USER32.dll", "GetForegroundWindow", h_GetForegroundWindow);
    hook_iat(exe, "USER32.dll", "WindowFromPoint", h_WindowFromPoint);
    hook_iat(exe, "USER32.dll", "GetActiveWindow", h_GetActiveWindow);
    hook_iat(exe, "USER32.dll", "GetFocus", h_GetFocus);
    hook_iat(exe, "USER32.dll", "SetCursorPos", h_SetCursorPos);
    hook_iat(exe, "USER32.dll", "ClipCursor", h_ClipCursor);
    hook_iat(exe, "USER32.dll", "SetForegroundWindow", h_SetForegroundWindow);
    hook_iat(exe, "USER32.dll", "SetActiveWindow", h_SetActiveWindow);
    hook_iat(exe, "USER32.dll", "SetFocus", h_SetFocus);
    hook_iat(exe, "USER32.dll", "GetKeyState", h_GetKeyState);
    hook_iat(exe, "USER32.dll", "GetAsyncKeyState", h_GetAsyncKeyState);
    hook_iat(exe, "USER32.dll", "PeekMessageA", h_PeekMessageA);
}
