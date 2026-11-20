/* test build only: record everything the game sends, to check that the patch never changes outgoing data.
   edjp_net.bin next to the DLL: records {u32 kind, u32 len, u32 tick, u32 port} + data
   kind 1 = OpenSSL SSL_write plaintext (HTTPS), 2 = send, 3 = sendto, 4 = WSASendTo */
#include <winsock2.h>
#include <intrin.h>
static CRITICAL_SECTION g_netcs;
static HANDLE g_netf = INVALID_HANDLE_VALUE;

static void netrec(DWORD kind, const void *p, DWORD len, DWORD port) {
    DWORD h[4], w;
    if (g_netf == INVALID_HANDLE_VALUE || !p || (int)len <= 0) return;
    h[0] = kind; h[1] = len; h[2] = GetTickCount(); h[3] = port;
    EnterCriticalSection(&g_netcs);
    WriteFile(g_netf, h, sizeof h, &w, NULL);
    WriteFile(g_netf, p, len, &w, NULL);
    LeaveCriticalSection(&g_netcs);
}
static DWORD sa_port(const struct sockaddr *a) {
    return a && (a->sa_family == AF_INET || a->sa_family == AF_INET6) ? ntohs(((struct sockaddr_in *)a)->sin_port) : 0;
}

/* log each distinct call stack once (exe RVAs) to find where the data is built / encrypted */
static void netstack(DWORD kind) {
    static unsigned long long seen[256]; static int nseen;
    void *fr[32]; char line[1024]; int n, i, o = 0;
    unsigned long long h = 1469598103934665603ULL;
    n = RtlCaptureStackBackTrace(2, 32, fr, NULL);
    for (i = 0; i < n; i++) h = (h ^ (unsigned long long)fr[i]) * 1099511628211ULL;
    EnterCriticalSection(&g_netcs);
    for (i = 0; i < nseen; i++) if (seen[i] == (h ^ kind)) break;
    if (i == nseen && nseen < 256) {
        seen[nseen++] = h ^ kind;
        for (i = 0; i < n && o < 1000; i++) {
            unsigned long long a = (unsigned long long)fr[i], b = (unsigned long long)g_exe;
            if (a >= b && a < b + 0x8000000) o += sprintf(line + o, " %llx", a - b); else o += sprintf(line + o, " *%llx", a);
        }
        dlog("stack kind %u:%s", kind, line);
    }
    LeaveCriticalSection(&g_netcs);
}

static int (*o_sslw)(void *, const void *, int);
static int h_sslw(void *s, const void *buf, int num) { netrec(1, buf, num, 0); netstack(1); return o_sslw(s, buf, num); }

static int WSAAPI h_send(SOCKET s, const char *b, int n, int f) { netrec(2, b, n, 0); return send(s, b, n, f); }
static int WSAAPI h_sendto(SOCKET s, const char *b, int n, int f, const struct sockaddr *to, int tl) {
    netrec(3, b, n, sa_port(to)); return sendto(s, b, n, f, to, tl);
}
static int WSAAPI h_WSASendTo(SOCKET s, LPWSABUF bufs, DWORD nb, LPDWORD sent, DWORD f, const struct sockaddr *to, int tl,
                              LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cr) {
    DWORD i;
    static volatile LONG nlog;
    for (i = 0; i < nb; i++) netrec(4, bufs[i].buf, bufs[i].len, sa_port(to));
    if (InterlockedIncrement(&nlog) <= 40) dlog("WSASendTo buf %p len %lu (nb %lu)", bufs[0].buf, bufs[0].len, nb);
    netstack(4);
    return WSASendTo(s, bufs, nb, sent, f, to, tl, ov, cr);
}

/* plaintext before the game's own encryption (4.4.2.1 RVAs, test only):
   0x4c47280 = UDP packet AES-CTR xor in place (this, buf, len, r9w, key): kind 5, port field = caller RVA
   0x545a00  = HTTP query/body encrypt (out, &handle, key, ...); handle+8 -> {+0x10 len, +0x18 data}: kind 6 */
static unsigned long long (*o_udpx)(void *, uint8_t *, size_t, unsigned long long, void *);
static unsigned long long h_udpx(void *t, uint8_t *b, size_t n, unsigned long long r9, void *k) {
    netrec(5, b, (DWORD)n, (DWORD)((uint8_t *)_ReturnAddress() - g_exe));
    return o_udpx(t, b, n, r9, k);
}
static unsigned long long (*o_httpx)(void *, void **, void *, void *);
static unsigned long long h_httpx(void *o, void **h, void *k, void *r9) {
    __try {
        uint8_t *H = h ? (uint8_t *)*h : NULL;
        if (H) {
            uint8_t *O = *(uint8_t **)(H + 8);
            size_t n = *(size_t *)(O + 0x10);
            uint8_t *d = *(uint8_t **)(O + 0x18);
            if (n < (1 << 24)) netrec(6, d, (DWORD)n, (DWORD)((uint8_t *)_ReturnAddress() - g_exe));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { dlog("httpx dump fault"); }
    return o_httpx(o, h, k, r9);
}

static void netlog_init(HMODULE exe) {
    /* SSL_write (OpenSSL 1.0.1q, static): mov eax,38h; call __chkstk; sub rsp,rax; cmp qword [rcx+30h],0; jne ...; mov edx,0D0h
       The call can't be copied, so the trampoline does "mov eax,38h; sub rsp,38h; cmp qword [rcx+30h],0" (same effect;
       __chkstk does nothing for 38h and keeps rax) and jumps to target+18, whose jne uses those flags. */
    uint8_t *t = find_pat((uint8_t *)exe, "B8 38 00 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 83 79 30 00 75 ?? BA D0 00 00 00");
    wchar_t fn[MAX_PATH];
    InitializeCriticalSection(&g_netcs);
    lstrcpyW(fn, g_dir); lstrcatW(fn, L"edjp_net.bin");
    g_netf = CreateFileW(fn, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (t) {
        static const uint8_t pre[] = { 0xB8, 0x38, 0, 0, 0, 0x48, 0x83, 0xEC, 0x38, 0x48, 0x83, 0x79, 0x30, 0x00 };
        uint8_t *tr = (uint8_t *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        DWORD old;
        memcpy(tr, pre, sizeof pre);
        tr[14] = 0xff; tr[15] = 0x25; *(DWORD *)(tr + 16) = 0; *(void **)(tr + 20) = t + 18;
        o_sslw = (int (*)(void *, const void *, int))tr;
        VirtualProtect(t, 18, PAGE_EXECUTE_READWRITE, &old);
        t[0] = 0xff; t[1] = 0x25; *(DWORD *)(t + 2) = 0; *(void **)(t + 6) = (void *)h_sslw;
        t[14] = t[15] = t[16] = t[17] = 0x90;
        VirtualProtect(t, 18, old, &old);
        FlushInstructionCache(GetCurrentProcess(), t, 18);
        dlog("SSL_write hooked at %llx", (unsigned long long)(t - (uint8_t *)exe));
    }
    /* the exe imports send/sendto by ordinal (19/20): swap the IAT slots that hold the real addresses */
    {
        void *real_send = (void *)GetProcAddress(GetModuleHandleW(L"ws2_32.dll"), "send");
        void *real_sendto = (void *)GetProcAddress(GetModuleHandleW(L"ws2_32.dll"), "sendto");
        BYTE *b = (BYTE *)exe;
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
        IMAGE_IMPORT_DESCRIPTOR *id = (IMAGE_IMPORT_DESCRIPTOR *)(b + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
        int n = 0;
        for (; id->Name; id++) {
            ULONG_PTR *a;
            if (lstrcmpiA((char *)(b + id->Name), "WS2_32.dll")) continue;
            for (a = (ULONG_PTR *)(b + id->FirstThunk); *a; a++) {
                void *h = (void *)*a == real_send ? (void *)h_send : (void *)*a == real_sendto ? (void *)h_sendto : NULL;
                DWORD old;
                if (!h) continue;
                VirtualProtect(a, 8, PAGE_READWRITE, &old); *a = (ULONG_PTR)h; VirtualProtect(a, 8, old, &old); n++;
            }
        }
        dlog("iat ws2 send/sendto by address: %d", n);
    }
    hook_iat(exe, "WS2_32.dll", "WSASendTo", (void *)h_WSASendTo);
    {
        uint8_t *u = (uint8_t *)exe + 0x4c47280, *x = (uint8_t *)exe + 0x545a00;
        static const uint8_t ub[] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x54 };
        static const uint8_t xb[] = { 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x48, 0x81, 0xEC, 0xF8, 0, 0, 0 };
        if (!memcmp(u, ub, sizeof ub)) { o_udpx = hook_at(u, 14, (void *)h_udpx); dlog("udp xor hooked"); } else dlog("udp xor bytes differ");
        if (!memcmp(x + 1, xb, sizeof xb) && x[0] == 0x40) { o_httpx = hook_at(x, 17, (void *)h_httpx); dlog("http encrypt hooked"); } else dlog("http encrypt bytes differ %02x %02x", x[0], x[1]);
    }
}
