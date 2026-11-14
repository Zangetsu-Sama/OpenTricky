/* win32_compat_ext.c - see win32_compat_ext.h. */
#include "win32_compat.h"
#include <errno.h>
#include <execinfo.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <dlfcn.h>
#include <strings.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <CoreGraphics/CoreGraphics.h>
#endif

/* WaitOnAddress: 64 hashed buckets of (mutex, cond). A waiter re-reads the
 * address under the bucket lock, so a wake between the compare and the wait
 * is never lost; spurious wakes are allowed, as on Windows. */
#define WOA_BUCKETS 64
static struct { pthread_mutex_t m; pthread_cond_t c; } g_woa[WOA_BUCKETS];
static pthread_once_t g_woa_once = PTHREAD_ONCE_INIT;
static void woa_init(void)
{
    for (int i = 0; i < WOA_BUCKETS; i++) {
        pthread_mutex_init(&g_woa[i].m, NULL);
        pthread_cond_init(&g_woa[i].c, NULL);
    }
}
static unsigned woa_bucket(const volatile void *a)
{
    uintptr_t x = (uintptr_t)a;
    return (unsigned)((x >> 3) ^ (x >> 9)) % WOA_BUCKETS;
}
static int woa_equal(const volatile void *a, const void *cmp, SIZE_T n)
{
    switch (n) {
    case 1: return __atomic_load_n((const volatile uint8_t *)a, __ATOMIC_SEQ_CST)  == *(const uint8_t *)cmp;
    case 2: return __atomic_load_n((const volatile uint16_t *)a, __ATOMIC_SEQ_CST) == *(const uint16_t *)cmp;
    case 4: return __atomic_load_n((const volatile uint32_t *)a, __ATOMIC_SEQ_CST) == *(const uint32_t *)cmp;
    default: return __atomic_load_n((const volatile uint64_t *)a, __ATOMIC_SEQ_CST) == *(const uint64_t *)cmp;
    }
}
BOOL WaitOnAddress(volatile VOID *addr, PVOID cmp, SIZE_T size, DWORD ms)
{
    pthread_once(&g_woa_once, woa_init);
    unsigned b = woa_bucket(addr);
    struct timespec ts;
    if (ms != INFINITE) {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += ms / 1000;
        ts.tv_nsec += (long)(ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    }
    BOOL ok = TRUE;
    pthread_mutex_lock(&g_woa[b].m);
    if (woa_equal(addr, cmp, size)) {
        int rc = (ms == INFINITE) ? pthread_cond_wait(&g_woa[b].c, &g_woa[b].m)
                                  : pthread_cond_timedwait(&g_woa[b].c, &g_woa[b].m, &ts);
        if (rc == ETIMEDOUT) { ok = FALSE; SetLastError(1460u /* ERROR_TIMEOUT */); }
    }
    pthread_mutex_unlock(&g_woa[b].m);
    return ok;
}
VOID WakeByAddressAll(PVOID addr)
{
    pthread_once(&g_woa_once, woa_init);
    unsigned b = woa_bucket(addr);
    pthread_mutex_lock(&g_woa[b].m);
    pthread_cond_broadcast(&g_woa[b].c);
    pthread_mutex_unlock(&g_woa[b].m);
}
/* One bucket holds several addresses: waking one waiter could pick the wrong
 * one, so "single" wakes them all (allowed: waiters re-check their value). */
VOID WakeByAddressSingle(PVOID addr) { WakeByAddressAll(addr); }

HANDLE CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD access)
{
    (void)access;
    return CreateWaitableTimerW(sa, (flags & CREATE_WAITABLE_TIMER_MANUAL_RESET) != 0, name);
}

DWORD GetCurrentProcessorNumber(void)
{
#if defined(__APPLE__)
    size_t n = 0;
    if (pthread_cpu_number_np(&n) == 0) return (DWORD)n;
    return 0;
#elif defined(__linux__)
    extern int sched_getcpu(void);
    int c = sched_getcpu();
    return c < 0 ? 0 : (DWORD)c;
#else
    return 0;
#endif
}

USHORT CaptureStackBackTrace(ULONG skip, ULONG count, PVOID *frames, PULONG hash)
{
    void *tmp[128];
    int n = backtrace(tmp, 128);
    int got = 0;
    for (int i = (int)skip + 1; i < n && got < (int)count; i++) frames[got++] = tmp[i];
    if (hash) {
        ULONG h = 0;
        for (int i = 0; i < got; i++) h = h * 31u + (ULONG)(uintptr_t)frames[i];
        *hash = h;
    }
    return (USHORT)got;
}

/* The main executable's image (the crash and profile code takes addresses
 * relative to it). Other modules are not looked up. */
static HMODULE self_module(void)
{
    Dl_info di;
    if (dladdr((void *)&self_module, &di) && di.dli_fbase) return (HMODULE)di.dli_fbase;
    return NULL;
}
HMODULE GetModuleHandleA(LPCSTR name) { return name ? NULL : self_module(); }
HMODULE GetModuleHandleW(LPCWSTR name) { return name ? NULL : self_module(); }

DWORD GetTempPathA(DWORD n, LPSTR buf)
{
    const char *t = getenv("TMPDIR");
    if (!t || !*t) t = "/tmp";
    size_t len = strlen(t);
    int slash = t[len - 1] != '/';
    if (len + slash + 1 > n) return (DWORD)(len + slash + 1);
    memcpy(buf, t, len);
    if (slash) buf[len++] = '/';
    buf[len] = 0;
    return (DWORD)len;
}

BOOL CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES sa)
{
    (void)sa;
    if (mkdir(path, 0755) == 0) return TRUE;
    SetLastError(errno == EEXIST ? ERROR_ALREADY_EXISTS : ERROR_PATH_NOT_FOUND);
    return FALSE;
}

DWORD GetModuleFileNameA(HMODULE m, LPSTR buf, DWORD n)
{
    (void)m;
    if (!n) return 0;
    char tmp[4096];
#if defined(__APPLE__)
    uint32_t sz = sizeof tmp;
    if (_NSGetExecutablePath(tmp, &sz) != 0) return 0;
    char real[4096];
    if (realpath(tmp, real)) snprintf(tmp, sizeof tmp, "%s", real);
#else
    ssize_t r = readlink("/proc/self/exe", tmp, sizeof tmp - 1);
    if (r <= 0) return 0;
    tmp[r] = 0;
#endif
    size_t len = strlen(tmp);
    if (len >= n) { memcpy(buf, tmp, n - 1); buf[n - 1] = 0; SetLastError(ERROR_INSUFFICIENT_BUFFER); return n; }
    memcpy(buf, tmp, len + 1);
    return (DWORD)len;
}

DWORD GetFileAttributesA(LPCSTR path)
{
    struct stat st;
    if (stat(path, &st) != 0) return INVALID_FILE_ATTRIBUTES;
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}

VOID GetSystemInfo(LPSYSTEM_INFO si)
{
    memset(si, 0, sizeof *si);
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    si->dwNumberOfProcessors = n > 0 ? (DWORD)n : 1;
    si->dwPageSize = (DWORD)getpagesize();
    si->dwAllocationGranularity = 65536;
    si->lpMinimumApplicationAddress = (LPVOID)(uintptr_t)0x10000;
    si->lpMaximumApplicationAddress = (LPVOID)(uintptr_t)0x7FFFFFFEFFFFull;
}

/* The game's table (weak: a program without resources links too). */
extern const xr_embedded_resource xr_embedded_resources[] __attribute__((weak));
extern const unsigned int xr_embedded_resource_count __attribute__((weak));

HRSRC FindResourceA(HMODULE m, LPCSTR name, LPCSTR type)
{
    (void)m; (void)type;
    if (!name || (uintptr_t)name < 0x10000 || !xr_embedded_resources || !&xr_embedded_resource_count)
        return NULL;
    for (unsigned i = 0; i < xr_embedded_resource_count; i++)
        if (strcasecmp(xr_embedded_resources[i].name, name) == 0)
            return (HRSRC)&xr_embedded_resources[i];
    return NULL;
}
HGLOBAL LoadResource(HMODULE m, HRSRC r) { (void)m; return (HGLOBAL)r; }
LPVOID LockResource(HGLOBAL g)
{ return g ? (LPVOID)((const xr_embedded_resource *)g)->data : NULL; }
DWORD SizeofResource(HMODULE m, HRSRC r)
{ (void)m; return r ? ((const xr_embedded_resource *)r)->size : 0; }

static void main_display_size(DWORD *w, DWORD *h, DWORD *hz)
{
    *w = 1920; *h = 1080; *hz = 60;
#if defined(__APPLE__)
    {
        CGDirectDisplayID d = CGMainDisplayID();
        CGDisplayModeRef m = CGDisplayCopyDisplayMode(d);
        if (m) {
            /* the size in points (what a window is measured in) */
            *w = (DWORD)CGDisplayModeGetWidth(m);
            *h = (DWORD)CGDisplayModeGetHeight(m);
            double r = CGDisplayModeGetRefreshRate(m);
            if (r > 1.0) *hz = (DWORD)(r + 0.5);
            CGDisplayModeRelease(m);
        }
    }
#endif
}

BOOL EnumDisplaySettingsW(LPCWSTR dev, DWORD mode, DEVMODEW *dm)
{
    (void)dev; (void)mode;
    if (!dm) return FALSE;
    main_display_size(&dm->dmPelsWidth, &dm->dmPelsHeight, &dm->dmDisplayFrequency);
    dm->dmBitsPerPel = 32;
    return TRUE;
}

BOOL SystemParametersInfoW(UINT action, UINT param, PVOID out, UINT winini)
{
    (void)param; (void)winini;
    if (action == SPI_GETWORKAREA && out) {
        DWORD w, h, hz;
        RECT *r = (RECT *)out;
        main_display_size(&w, &h, &hz);
        r->left = 0; r->top = 0;
        r->right = (LONG)w;
        r->bottom = (LONG)h - 64;        /* the menu bar and the Dock, roughly */
        return TRUE;
    }
    return FALSE;
}

BOOL AdjustWindowRect(RECT *r, DWORD style, BOOL menu)
{
    (void)style; (void)menu;
    if (r) r->top -= 28;                 /* the title bar */
    return TRUE;
}

DWORD GetFullPathNameA(LPCSTR path, DWORD n, LPSTR out, LPSTR *file_part)
{
    char tmp[4096], *c;
    snprintf(tmp, sizeof tmp, "%s", path);
    for (c = tmp; *c; c++) if (*c == '\\') *c = '/';
    char real[4096];
    const char *res = realpath(tmp, real) ? real : tmp;
    size_t len = strlen(res);
    if (file_part) *file_part = NULL;
    if (len + 1 > n) return (DWORD)(len + 1);
    memcpy(out, res, len + 1);
    return (DWORD)len;
}

/* Weak: the window layer (the SDL window loop) has the real one. */
__attribute__((weak)) HWND GetForegroundWindow(void) { return NULL; }
