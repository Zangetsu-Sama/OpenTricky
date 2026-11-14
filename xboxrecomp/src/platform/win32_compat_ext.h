/*
 * win32_compat_ext.h - the Win32 calls OpenTricky's runtime uses on top of the
 * shared POSIX layer (win32_compat.h): address waits, waitable timers created
 * with the Ex form, stack captures, a few path helpers. macOS and Linux.
 */
#ifndef WIN32_COMPAT_EXT_H
#define WIN32_COMPAT_EXT_H
#if !defined(_WIN32)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- atomics ----------------------------------------------------------- */
static inline LONG64 InterlockedIncrement64(volatile LONG64 *a)
{ return __atomic_add_fetch(a, 1, __ATOMIC_SEQ_CST); }
static inline LONG64 InterlockedDecrement64(volatile LONG64 *a)
{ return __atomic_sub_fetch(a, 1, __ATOMIC_SEQ_CST); }
static inline LONG64 InterlockedExchange64(volatile LONG64 *t, LONG64 v)
{ return __atomic_exchange_n(t, v, __ATOMIC_SEQ_CST); }
static inline LONG64 InterlockedExchangeAdd64(volatile LONG64 *t, LONG64 v)
{ return __atomic_fetch_add(t, v, __ATOMIC_SEQ_CST); }
static inline PVOID InterlockedExchangePointer(PVOID volatile *t, PVOID v)
{ return __atomic_exchange_n(t, v, __ATOMIC_SEQ_CST); }
static inline LONG InterlockedOr(volatile LONG *t, LONG v)
{ return __atomic_fetch_or(t, v, __ATOMIC_SEQ_CST); }
static inline LONG InterlockedAnd(volatile LONG *t, LONG v)
{ return __atomic_fetch_and(t, v, __ATOMIC_SEQ_CST); }
#ifndef MemoryBarrier
#define MemoryBarrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif
#ifndef _ReadWriteBarrier
#define _ReadWriteBarrier() __asm__ volatile("" ::: "memory")
#endif

/* The 4 GB guest window, reserved whole (xbox_memory_layout.c, macOS). */
void xbox_compat_guest_window(void *lo, size_t size);

/* ---- WaitOnAddress ----------------------------------------------------- */
BOOL WaitOnAddress(volatile VOID *addr, PVOID cmp, SIZE_T size, DWORD ms);
VOID WakeByAddressAll(PVOID addr);
VOID WakeByAddressSingle(PVOID addr);

/* ---- waitable timers, Ex form ------------------------------------------ */
#define CREATE_WAITABLE_TIMER_MANUAL_RESET     0x00000001u
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION  0x00000002u
#define TIMER_ALL_ACCESS                       0x001F0003u
#define TIMER_MODIFY_STATE                     0x00000002u
HANDLE CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD access);

/* ---- processors, modules, stacks --------------------------------------- */
DWORD  GetCurrentProcessorNumber(void);
USHORT CaptureStackBackTrace(ULONG skip, ULONG count, PVOID *frames, PULONG hash);
HMODULE GetModuleHandleA(LPCSTR name);
HMODULE GetModuleHandleW(LPCWSTR name);

/* ---- paths -------------------------------------------------------------- */
DWORD GetTempPathA(DWORD n, LPSTR buf);
BOOL  CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES sa);

/* ---- CRT ---------------------------------------------------------------- */
#define _fseeki64 fseeko
#define _ftelli64 ftello


/* ---- more modules, memory, files, system ------------------------------- */
#ifndef GetModuleHandle
#define GetModuleHandle GetModuleHandleA
#endif
DWORD GetModuleFileNameA(HMODULE m, LPSTR buf, DWORD n);
#ifndef MEM_IMAGE
#define MEM_IMAGE  0x01000000u
#endif
#ifndef MEM_MAPPED
#define MEM_MAPPED 0x00040000u
#endif
static inline BOOL HeapValidate(HANDLE heap, DWORD flags, LPCVOID p)
{ (void)heap; (void)flags; (void)p; return TRUE; }
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#endif
DWORD GetFileAttributesA(LPCSTR path);
typedef struct _SYSTEM_INFO {
    WORD  wProcessorArchitecture;
    WORD  wReserved;
    DWORD dwPageSize;
    LPVOID lpMinimumApplicationAddress;
    LPVOID lpMaximumApplicationAddress;
    DWORD_PTR dwActiveProcessorMask;
    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD  wProcessorLevel;
    WORD  wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;
VOID GetSystemInfo(LPSYSTEM_INFO si);

/* ---- display (the main display's size; macOS: CoreGraphics) ------------- */
#define ENUM_CURRENT_SETTINGS ((DWORD)-1)
typedef struct _DEVMODEW {
    WORD  dmSize;
    DWORD dmBitsPerPel;
    DWORD dmPelsWidth;
    DWORD dmPelsHeight;
    DWORD dmDisplayFrequency;
} DEVMODEW, DEVMODEA;
BOOL EnumDisplaySettingsW(LPCWSTR dev, DWORD mode, DEVMODEW *dm);
#define EnumDisplaySettingsA(d, m, dm) EnumDisplaySettingsW(NULL, (m), (dm))
#define SPI_GETWORKAREA 0x0030
BOOL SystemParametersInfoW(UINT action, UINT param, PVOID out, UINT winini);
#define WS_OVERLAPPEDWINDOW 0x00CF0000u
BOOL AdjustWindowRect(RECT *r, DWORD style, BOOL menu);

/* ---- CRT ---------------------------------------------------------------- */
static inline int _putenv_s(const char *name, const char *value)
{ return (value && *value) ? setenv(name, value, 1) : unsetenv(name); }
DWORD GetFullPathNameA(LPCSTR path, DWORD n, LPSTR out, LPSTR *file_part);
#ifndef PAGE_GUARD
#define PAGE_GUARD 0x100u
#endif

/* ---- the virtual keys the controls use beyond win32_compat.h ------------ */
#define VK_LSHIFT   0xA0
#define VK_RSHIFT   0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU    0xA4
#define VK_RMENU    0xA5
#define VK_NUMPAD0  0x60
#define VK_NUMPAD1  0x61
#define VK_NUMPAD2  0x62
#define VK_NUMPAD3  0x63
#define VK_NUMPAD4  0x64
#define VK_NUMPAD5  0x65
#define VK_NUMPAD6  0x66
#define VK_NUMPAD7  0x67
#define VK_NUMPAD8  0x68
#define VK_NUMPAD9  0x69
#define VK_DECIMAL  0x6E
#define VK_OEM_1    0xBA
#define VK_OEM_2    0xBF
#define VK_OEM_3    0xC0
#define VK_OEM_4    0xDB
#define VK_OEM_5    0xDC
#define VK_OEM_6    0xDD
#define VK_OEM_7    0xDE
#ifndef VK_F6
#define VK_F6  0x75
#define VK_F7  0x76
#define VK_F8  0x77
#define VK_F9  0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#endif

/* ---- timers, console, keyboard, windows -------------------------------- */
#ifndef TIMERR_NOERROR
#define TIMERR_NOERROR 0
#endif
static inline UINT timeBeginPeriod(UINT ms) { (void)ms; return TIMERR_NOERROR; }
static inline UINT timeEndPeriod(UINT ms) { (void)ms; return TIMERR_NOERROR; }
static inline BOOL SetConsoleTitleA(LPCSTR t) { (void)t; return TRUE; }
#define MAPVK_VK_TO_VSC    0
#define MAPVK_VSC_TO_VK    1
#define MAPVK_VK_TO_CHAR   2
#define MAPVK_VSC_TO_VK_EX 3
static inline UINT MapVirtualKeyW(UINT code, UINT type) { (void)code; (void)type; return 0; }
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_F13 0x7C
#define VK_F24 0x87
/* The window with the keyboard focus: the game window when it has it (the
 * window layer defines it; NULL otherwise). */
HWND GetForegroundWindow(void);

/* ---- resources ----------------------------------------------------------
 * The executable's RCDATA: on Windows they are in the .rc; here a table the
 * game build generates from the same .rc (port/tools/rc_embed.py). */
typedef struct xr_embedded_resource {
    const char *name;
    const unsigned char *data;
    unsigned int size;
} xr_embedded_resource;
typedef void *HRSRC;
#ifndef MAKEINTRESOURCEA
#define MAKEINTRESOURCEA(i) ((LPSTR)(uintptr_t)(WORD)(i))
#endif
#ifndef RT_RCDATA
#define RT_RCDATA MAKEINTRESOURCEA(10)
#endif
HRSRC   FindResourceA(HMODULE m, LPCSTR name, LPCSTR type);
HGLOBAL LoadResource(HMODULE m, HRSRC r);
LPVOID  LockResource(HGLOBAL g);
DWORD   SizeofResource(HMODULE m, HRSRC r);

#ifdef __cplusplus
}
#endif
#endif /* !_WIN32 */
#endif
