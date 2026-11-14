/* SPDX-License-Identifier: GPL-3.0-only
 *
 * posix_signals.c -- the POSIX side of crashreport.h and of the trapped
 * device registers, after SSX3-recomp's macOS port (its main.c segv_handler).
 *
 * On Windows the game's vectored handler services the three trapped register
 * pages (AC'97, the MCPX APU, the NV2A palette) and crashreport.c writes the
 * crash report. Here one signal handler does both: an access to a trapped
 * page is decoded (mmio_posix.c: the host arm64 / x86-64 load or store),
 * handed to the device and stepped over; anything else is a crash, reported
 * to the log with the guest registers and a native backtrace.
 */
#ifndef _WIN32
#include <execinfo.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include "win32_compat.h"
#include "uc_regs.h"
#include "mmio_posix.h"
#include "../crashreport.h"
#include "apu/aci_mmio.h"
#include "apu/apu.h"
#include "nv2a/nv2a_mmio_hook.h"

extern ptrdiff_t g_xbox_mem_offset;
extern __thread uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
extern volatile unsigned g_last_loc;

#define ACI_BASE   0xFEC00000u
#define ACI_SIZE   0x00001000u
#define APU_BASE   0xFE800000u
#define APU_SIZE   0x00080000u
#define DAC_BASE   (0xFD000000u + 0x00680000u)   /* NV2A PRMDIO, in its 16 KB host page */
#define DAC_SIZE   0x00004000u

static uint32_t to_va(uintptr_t host) { return (uint32_t)(host - (uintptr_t)g_xbox_mem_offset); }

static int owned(uint32_t va)
{
    return (va >= ACI_BASE && va < ACI_BASE + ACI_SIZE) ||
           (va >= APU_BASE && va < APU_BASE + APU_SIZE) ||
           (va >= DAC_BASE && va < DAC_BASE + DAC_SIZE);
}

static bool device_access(uint32_t va, int is_write, unsigned size, uint64_t *v)
{
    if (va >= ACI_BASE && va < ACI_BASE + ACI_SIZE) return aci_mmio_access(va, is_write, size, v);
    if (va >= APU_BASE && va < APU_BASE + APU_SIZE) return apu_mmio_access(va, is_write, size, v);
    if (va >= DAC_BASE && va < DAC_BASE + DAC_SIZE) return nv2a_dac_access(va, is_write, size, v);
    return false;
}

static uint64_t dev_read(uintptr_t addr, int size)
{
    uint64_t v = 0;
    device_access(to_va(addr), 0, (unsigned)size, &v);
    return v;
}

static void dev_write(uintptr_t addr, uint64_t val, int size)
{
    device_access(to_va(addr), 1, (unsigned)size, &val);
}

static volatile int s_crashing;

static void fault_handler(int sig, siginfo_t *si, void *uc_)
{
    ucontext_t *uc = (ucontext_t *)uc_;
    uintptr_t fault = (uintptr_t)si->si_addr;
    uint32_t insn = 0;
    void *bt[48];
    int n;
#if defined(__APPLE__)
    int is_fault = (sig == SIGSEGV || sig == SIGBUS);   /* Darwin: a protected page is SIGBUS */
#else
    int is_fault = (sig == SIGSEGV);
#endif

    if (is_fault && g_xbox_mem_offset && owned(to_va(fault)) &&
        mmio_posix_step(uc, fault, dev_read, dev_write, &insn))
        return;

    if (__sync_lock_test_and_set(&s_crashing, 1)) _exit(139);
    fprintf(stderr, "CRASH: signal %d at host pc=%p, fault addr=%p, insn %08X "
                    "[last label stamped by any thread: loc_%08X]\n",
            sig, (void *)(uintptr_t)UC_PC(uc), (void *)fault, insn, g_last_loc);
    if (g_xbox_mem_offset)
        fprintf(stderr, "CRASH: Xbox VA of the fault 0x%08X\n", to_va(fault));
    fprintf(stderr, "Guest:     eax=%08X ecx=%08X edx=%08X ebx=%08X\n"
                    "           esp=%08X esi=%08X edi=%08X\n",
            g_eax, g_ecx, g_edx, g_ebx, g_esp, g_esi, g_edi);
    n = backtrace(bt, 48);
    backtrace_symbols_fd(bt, n, 2);
    fflush(stdout);
    fflush(stderr);
    _exit(139);
}

void crashreport_install(void)
{
    static uint8_t *alt;
    stack_t ss;
    struct sigaction sa;

    /* an alternate stack: a guest stack overflow still gets its report */
    if (!alt) alt = (uint8_t *)malloc(1u << 20);
    if (alt) {
        ss.ss_sp = alt;
        ss.ss_size = 1u << 20;
        ss.ss_flags = 0;
        sigaltstack(&ss, NULL);
    }
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = fault_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
}

/* Each thread needs the alternate stack of its own (sigaltstack is per
 * thread); the kernel's thread wrapper is not ours, so a thread without one
 * reports on its own stack, which works for everything but an overflow. */

void crashreport_set_output(int player, const char *log_path, int output_redirected)
{ (void)player; (void)log_path; (void)output_redirected; }
void crashreport_game_start(void) {}
/* Direct3D 11's failure report (crashreport.c): never called off Windows. */
int crashreport_on_gpu_failure(int lost, const char *details, wchar_t *dir_out, size_t dir_out_len)
{ (void)lost; (void)details; (void)dir_out; (void)dir_out_len; return 0; }
void crashreport_gpu_dialog(int lost, const char *what, unsigned long hr, unsigned long reason, const wchar_t *dir)
{ (void)lost; (void)what; (void)hr; (void)reason; (void)dir; }
void crashreport_private_folder(const char *utf8) { (void)utf8; }
void crashreport_private_file_acp(const char *path) { (void)path; }

size_t crashreport_private_text(const char *src, size_t n, int ansi, char *dst, size_t cap)
{
    (void)ansi;
    if (!cap) return 0;
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
    return n;
}

int crashreport_on_crash(EXCEPTION_POINTERS *ep, const char *details,
                         wchar_t *dir_out, size_t dir_out_len)
{ (void)ep; (void)details; (void)dir_out; (void)dir_out_len; return 0; }
void crashreport_fatal_overflow(EXCEPTION_POINTERS *ep) { (void)ep; }
void crashreport_crash_dialog(DWORD code, const wchar_t *dir) { (void)code; (void)dir; }
void crashreport_open_folder(const wchar_t *dir) { (void)dir; }
void crashreport_hold(unsigned ms) { (void)ms; }
#endif /* !_WIN32 */
