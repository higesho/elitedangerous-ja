/* TEST ONLY: hardware data breakpoint on one address (all threads); every hit logs the
   instruction and a few return addresses (as exe RVAs) to the log, deduplicated. */
static volatile ULONG64 w_addr; static volatile int w_len, w_rw;
static volatile LONG w_hits;
#define W_MAXSIG 400
static ULONG64 w_sig[W_MAXSIG]; static volatile LONG w_nsig;

static ULONG64 rva(ULONG64 a) {
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(g_exe + ((IMAGE_DOS_HEADER *)g_exe)->e_lfanew);
    if (a >= (ULONG64)g_exe && a < (ULONG64)g_exe + nt->OptionalHeader.SizeOfImage) return a - (ULONG64)g_exe;
    return a | 0x8000000000000000ull;
}

static LONG CALLBACK w_veh(EXCEPTION_POINTERS *ep) {
    CONTEXT c;
    ULONG64 frames[8], sig = 0;
    int i, n = 0;
    LONG k;
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    if (!(ep->ContextRecord->Dr6 & 0xf)) return EXCEPTION_CONTINUE_SEARCH;
    ep->ContextRecord->Dr6 = 0;
    if (!((ep->ContextRecord->Dr7 >> 16) & 3)) ep->ContextRecord->EFlags |= 0x10000;   /* resume flag for execute breakpoints */
    c = *ep->ContextRecord;
    frames[n++] = c.Rip;
    for (i = 0; i < 7; i++) {
        ULONG64 ib; PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(c.Rip, &ib, NULL);
        void *hd; ULONG64 ef;
        if (!f) { c.Rip = *(ULONG64 *)c.Rsp; c.Rsp += 8; }
        else RtlVirtualUnwind(0, ib, c.Rip, f, &c, &hd, &ef, NULL);
        if (!c.Rip) break;
        frames[n++] = c.Rip;
    }
    for (i = 0; i < n; i++) sig = sig * 1000003 + frames[i];
    for (k = 0; k < w_nsig; k++) if (w_sig[k] == sig) break;
    InterlockedIncrement(&w_hits);
    if (k == w_nsig && w_nsig < W_MAXSIG) {
        char buf[400]; int p = 0;
        w_sig[InterlockedIncrement(&w_nsig) - 1] = sig;
        p += sprintf(buf + p, "WATCH hit %p tid %lu:", (void *)w_addr, GetCurrentThreadId());
        for (i = 0; i < n; i++) p += sprintf(buf + p, " %llx", rva(frames[i]));
        p += sprintf(buf + p, " | rax=%llx rcx=%llx rdx=%llx r8=%llx", ep->ContextRecord->Rax, ep->ContextRecord->Rcx, ep->ContextRecord->Rdx, ep->ContextRecord->R8);
        dlog("%s", buf);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void w_apply_thread(DWORD tid) {
    HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, tid);
    CONTEXT c;
    if (!t) return;
    SuspendThread(t);
    memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(t, &c)) {
        if (w_addr) {
            DWORD lenbits = w_len == 8 ? 2 : w_len == 4 ? 3 : w_len == 2 ? 1 : 0;
            DWORD rwbits = w_rw;                 /* 1 = write, 3 = read/write */
            c.Dr0 = w_addr;
            c.Dr7 = (c.Dr7 & ~0xf0003ull) | 1 | (rwbits << 16) | (lenbits << 18);
        } else {
            c.Dr0 = 0; c.Dr7 &= ~0xf0003ull;
        }
        SetThreadContext(t, &c);
    }
    ResumeThread(t);
    CloseHandle(t);
}

static DWORD WINAPI w_worker(LPVOID p) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te; int n = 0;
    te.dwSize = sizeof te;
    if (s != INVALID_HANDLE_VALUE && Thread32First(s, &te)) {
        do {
            if (te.th32OwnerProcessID == GetCurrentProcessId() && te.th32ThreadID != GetCurrentThreadId()) { w_apply_thread(te.th32ThreadID); n++; }
        } while (Thread32Next(s, &te));
        CloseHandle(s);
    }
    dlog("watch %p len %d rw %d applied to %d threads", (void *)w_addr, w_len, w_rw, n);
    return 0;
}

static void watch_set(ULONG64 addr, int len, int rw) {
    HANDLE t;
    w_addr = addr; w_len = len ? len : 8; w_rw = rw == 4 ? 0 : rw ? rw : 3; if (rw == 4) w_len = 1;
    w_nsig = 0; w_hits = 0;
    t = CreateThread(NULL, 0, w_worker, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

static LONG CALLBACK crash_veh(EXCEPTION_POINTERS *ep) {
    static volatile LONG n;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0xC0000005 || code == 0xC000001D || code == 0xC0000096 || code == 0xC0000374 || code == 0xC0000409) {
        if (InterlockedIncrement(&n) <= 30) {
            CONTEXT c = *ep->ContextRecord;
            char buf[600]; int p = 0, i;
            p += sprintf(buf + p, "EXC %08lx at %llx addr %llx:", code, rva(c.Rip),
                         ep->ExceptionRecord->NumberParameters > 1 ? (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1] : 0ull);
            for (i = 0; i < 10; i++) {
                ULONG64 ib; PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(c.Rip, &ib, NULL);
                void *hd; ULONG64 ef;
                if (!f) { c.Rip = *(ULONG64 *)c.Rsp; c.Rsp += 8; }
                else RtlVirtualUnwind(0, ib, c.Rip, f, &c, &hd, &ef, NULL);
                if (!c.Rip) break;
                p += sprintf(buf + p, " %llx", rva(c.Rip));
            }
            dlog("%s", buf);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void watch_init(void) {
    AddVectoredExceptionHandler(1, w_veh);
    AddVectoredExceptionHandler(0, crash_veh);
}
