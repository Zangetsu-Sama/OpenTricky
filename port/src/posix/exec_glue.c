/* SPDX-License-Identifier: GPL-3.0-only
 *
 * exec_glue.c -- macOS: what SSX3-recomp's executor (xboxrecomp/src/nv2a_exec)
 * and Vulkan backend (src/vk/vkr_backend.c) take from that project's kernel
 * and game, answered by OpenTricky's.
 *
 * OpenTricky maps guest RAM 1:1 (an NV2A offset is the guest VA of its
 * bytes; the 0x80000000 / 0xF0000000 aliases fold onto it, recomp_types.h),
 * its PFIFO pump already completes the title's fences, and the frame
 * boundary is pgraph_vk.c's. The SSX 3 game hooks (widescreen camera,
 * rider tags for the motion blur, autopilot) have no SSX Tricky meaning yet:
 * they answer "off".
 */
#ifndef _WIN32
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/sysctl.h>
#include "win32_compat.h"
#include "recomp/recomp_types.h"   /* xbox_resolve_uncached_alias */

extern ptrdiff_t g_xbox_mem_offset;

/* ── memory ──────────────────────────────────────────────────────────────── */
uint32_t g_xbox_image_lo, g_xbox_image_hi;          /* 0: the executor never refuses */
uint32_t xbox_ContiguousAllocatedBytes(void) { return 0; }   /* offsets are VAs */
void *xbox_GuestToHost(uint32_t va)
{
    return (void *)((uintptr_t)xbox_resolve_uncached_alias(va) + (uintptr_t)g_xbox_mem_offset);
}
uint32_t xbox_HostToGuest(const void *host)
{
    return (uint32_t)((uintptr_t)host - (uintptr_t)g_xbox_mem_offset);
}

/* ── GPU completion: OpenTricky's pump owns the fences ──────────────────── */
void xbox_Nv2aSemaphoreRelease(uint32_t value) { (void)value; }
void xbox_Nv2aSoftwareMethod(uint32_t subch, uint32_t method, uint32_t param)
{ (void)subch; (void)method; (void)param; }
int xbox_io_log(void) { return 0; }

uint32_t xbox_TickCountMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* ── SSX 3's game hooks in vkr_backend.c ─────────────────────────────────── */
volatile int g_ssx3_at_briefing;
int   ssx3_WidescreenNow(void) { return 0; }
int   ssx3_InGame(void) { return 0; }
int   ssx3_InGameMenuUp(void) { return 0; }
float ssx3_Aspect(void) { return 4.0f / 3.0f; }
void  ssx3_SetAspect(float a) { (void)a; }
int   ssx3_RiderPos(int i, float out[3]) { (void)i; (void)out; return 0; }
void  ssx3_PerfFlip(void) {}
int   xbox_PadInject(const char *line) { (void)line; return 0; }

/* ── toolchain ───────────────────────────────────────────────────────────── */
/* The PE image base (main.c's crash lines print addresses relative to it):
 * on macOS any fixed symbol does; the backtraces carry the real addresses. */
char __ImageBase;

/* Clang's @available check (SDL3's Objective-C uses it); the cross build has
 * no compiler-rt for macOS. macOS is platform 1. */
int32_t __isPlatformVersionAtLeast(uint32_t platform, uint32_t major, uint32_t minor, uint32_t sub)
{
    static int cached = 0, M, m, s;
    if (platform != 1) return 0;
    if (!cached) {
        char v[32] = {0};
        size_t n = sizeof v - 1;
        if (sysctlbyname("kern.osproductversion", v, &n, NULL, 0) == 0)
            sscanf(v, "%d.%d.%d", &M, &m, &s);
        cached = 1;
    }
    if ((uint32_t)M != major) return (uint32_t)M > major;
    if ((uint32_t)m != minor) return (uint32_t)m > minor;
    return (uint32_t)s >= sub;
}
#endif /* !_WIN32 */
