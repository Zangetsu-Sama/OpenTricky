/* SPDX-License-Identifier: GPL-3.0-only
 *
 * d3d8_posix.c -- the D3D8 host calls on macOS.
 *
 * On Windows d3d8_device.c owns the window, the D3D11 device and the extras
 * drawn over the game's picture (SMAA, bloom, soft shadows, render scale,
 * the display pacing gate). On macOS the window is the SDL window loop
 * (port/src/posix/fb_present_sdl3.c) and the picture is the NV2A executor's
 * Vulkan renderer (port/src/vk): what is left here are the calls the game and
 * the runtime make into this layer -- settings, the guest framebuffer the
 * videos are written to, the present counter, the exit hooks. The picture
 * extras answer "off".
 */
#ifndef _WIN32
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <unistd.h>
#include "win32_compat.h"

#include "d3d8_xbox.h"

extern void xbox_FramebufferPublishImage(const uint32_t *px, uint32_t w, uint32_t h);

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* ── the window: a stand-in handle the game compares with the focus ─────── */
static int s_window_token;
HWND d3d8_posix_window_handle(void) { return (HWND)&s_window_token; }
HWND d3d8_GetHostWindow(void) { return d3d8_posix_window_handle(); }
void d3d8_PumpMessages(void) {}                 /* the SDL loop has the events */

/* ── presents ─────────────────────────────────────────────────────────────── */
static volatile unsigned s_present_seq;
void d3d8_posix_note_present(void);
static volatile double s_present_at;
void d3d8_posix_note_present(void)
{
    __atomic_add_fetch(&s_present_seq, 1, __ATOMIC_RELAXED);
    s_present_at = now_ms();
}
unsigned d3d8_PresentSeq(void) { return s_present_seq; }
DWORD d3d8_MsSincePresent(void) { return s_present_at ? (DWORD)(now_ms() - s_present_at) : 0; }
double d3d8_MsSincePresentQpc(void) { return s_present_at ? now_ms() - s_present_at : 0.0; }
/* The pump's present (nv2a_live_pb.c: no flip for a while): what was drawn. */
__attribute__((weak)) void pgraph_vk_present_now(void) {}
void d3d8_PresentFrame(void) { pgraph_vk_present_now(); }
void d3d8_IdleFlush(void) {}

/* ── the guest framebuffer the videos are blitted into (recomp_manual.c) ───
 * As d3d8_device.c on Windows: the video player locks one of the two back
 * buffers before writing each frame into it; when the title then flips away
 * from a buffer locked since its last flip, that flip is a video frame, shown
 * from guest memory -- not the GPU's picture of it, which has none of the
 * CPU's pixels (an image of black, every other frame). */
static const uint8_t *s_gfb[2];
static unsigned s_gfb_pitch, s_gfb_w, s_gfb_h;
static volatile uint32_t s_gfb_va[2];
static volatile unsigned s_locks, s_written[2], s_handed[2];
uint32_t g_guest_fb_alt_va;

void d3d8_SetGuestFramebuffer(const void *src, unsigned pitch, unsigned w, unsigned h)
{
    s_gfb[0] = (const uint8_t *)src;
    s_gfb_pitch = pitch; s_gfb_w = w; s_gfb_h = h;
}
void d3d8_SetGuestFramebufferAlt(const void *alt) { s_gfb[1] = (const uint8_t *)alt; }
void d3d8_NoteGuestFramebufferLockAt(uint32_t va, const void *p)
{
    int i = !p ? -1 : p == s_gfb[0] ? 0 : p == s_gfb[1] ? 1 : -1;
    unsigned n = __atomic_add_fetch(&s_locks, 1, __ATOMIC_SEQ_CST);
    if (i >= 0) { s_gfb_va[i] = va; s_written[i] = n; }
}
int d3d8_HasGuestFramebuffer(void) { return s_gfb[0] != NULL; }
int d3d8_GuestFramebufferActive(void) { return s_gfb[0] != NULL; }
int d3d8_VideoFramePending(void) { return 0; }
int d3d8_VideoPresentReady(void) { return 1; }

static void publish_guest(const uint8_t *src)
{
    static uint32_t *px;
    unsigned y, x, w = s_gfb_w > 640 ? 640 : s_gfb_w, h = s_gfb_h > 480 ? 480 : s_gfb_h;
    if (!px) px = (uint32_t *)malloc((size_t)640 * 480 * 4);
    if (!px || !src) return;
    for (y = 0; y < h; y++) {
        uint32_t *d = px + (size_t)y * w;
        const uint32_t *s = (const uint32_t *)(src + (size_t)y * s_gfb_pitch);
        for (x = 0; x < w; x++) d[x] = s[x] | 0xFF000000u;
    }
    { extern int g_pub_src; g_pub_src = 2; }
    { extern void xbox_FramebufferSetBox(float k); xbox_FramebufferSetBox(1.0f); }   /* at its own 4:3 */
    xbox_FramebufferPublishImage(px, w, h);
    d3d8_posix_note_present();
}

/* The title's render surface leaves `offset` (pgraph_vk.c): 1 if that was a
 * video frame, now shown. */
int d3d8_posix_video_flip(uint32_t offset)
{
    int i;
    {
        static int left = -1;
        if (left < 0) left = getenv("OT_VIDEO_TRACE") ? 30 : 0;
        if (left > 0 && s_gfb[0]) {
            left--;
            fprintf(stderr, "[VIDEO] flip from %08X: buffers %08X %08X written %u %u handed %u %u\n",
                    offset, s_gfb_va[0], s_gfb_va[1], s_written[0], s_written[1], s_handed[0], s_handed[1]);
        }
    }
    if (!s_gfb[0] || !s_gfb_pitch) return 0;
    for (i = 0; i < 2; i++)
        if (s_gfb_va[i] && (s_gfb_va[i] & 0x0FFFFFFFu) == (offset & 0x0FFFFFFFu)) break;
    if (i == 2 || s_written[i] == s_handed[i]) return 0;     /* an ordinary flip */
    s_handed[i] = s_written[i];
    publish_guest(s_gfb[i]);
    return 1;
}

/* ── settings the launcher hands the display (not used by the Vulkan path) ─ */
void d3d8_SetShowFps(int on) { (void)on; }
/* The launcher's anti-aliasing and texture filtering, for the Vulkan
 * renderer (vkr.c reads RECOMP_MSAA, RECOMP_ANISO when it starts); a test
 * run's own RECOMP_* win. */
void d3d8_SetAnisotropy(int n)
{
    char v[16];
    snprintf(v, sizeof v, "%d", n > 1 ? (n > 16 ? 16 : n) : 0);
    setenv("RECOMP_ANISO", v, 0);
}
void d3d8_SetMsaa(int samples)
{
    char v[16];
    snprintf(v, sizeof v, "%d", samples == 2 || samples == 4 || samples == 8 ? samples : 0);
    setenv("RECOMP_MSAA", v, 0);
}
/* The screen's shape (aspect.c): the Vulkan target is made that wide
 * (vkr.c reads RECOMP_ASPECT); the title draws its wide view into it. */
static void set_aspect_env(double aspect, int overwrite)
{
    char v[32];
    if (!(aspect > 4.0 / 3.0 + 0.005)) return;
    snprintf(v, sizeof v, "%.6f", aspect);
    setenv("RECOMP_ASPECT", v, overwrite);
}
void d3d8_SetHostAspect(double aspect) { set_aspect_env(aspect, 1); }
void d3d8_SetHostDisplay(unsigned render_w, unsigned render_h, int widescreen, int fullscreen)
{
    char v[32];
    snprintf(v, sizeof v, "%ux%u", render_w, render_h);
    if (render_w >= 640 && render_h >= 480) setenv("XBOX_WINDOW_SIZE", v, 0);
    if (fullscreen) setenv("XBOX_FULLSCREEN", "1", 0);
    if (widescreen) set_aspect_env(16.0 / 9.0, 0);
}
void d3d8_SetPointZoom(float zoom) { (void)zoom; }
void d3d8_SetPresentBoxShape(double shape) { (void)shape; }
/* The DAC palette (the title's gamma ramp, nv2a_core.c): applied to the
 * shown image as d3d8_device.c does (XBOX_FIX_GAMMA=0: off). The window's
 * copy of each frame goes through it (fb_present_sdl3.c). */
static uint32_t s_dac_r[256], s_dac_g[256], s_dac_b[256];
static volatile int s_dac_active;
void d3d8_SetDacPalette(const unsigned char lut[256][3])
{
    static int fix = -1;
    int i, ident = 1;
    if (fix < 0) { const char *e = getenv("XBOX_FIX_GAMMA"); fix = !(e && e[0] == '0'); }
    for (i = 0; i < 256; i++) {
        s_dac_r[i] = (uint32_t)lut[i][0] << 16;
        s_dac_g[i] = (uint32_t)lut[i][1] << 8;
        s_dac_b[i] = lut[i][2];
        if (lut[i][0] != i || lut[i][1] != i || lut[i][2] != i) ident = 0;
    }
    s_dac_active = fix && !ident;
    if (getenv("OT_DAC_DUMP")) {    /* diagnostic: the table, and where it is not monotonic */
        int bad = 0;
        for (i = 1; i < 256; i++)
            if (lut[i][0] < lut[i - 1][0] || lut[i][1] < lut[i - 1][1] || lut[i][2] < lut[i - 1][2]) {
                if (bad++ < 12)
                    fprintf(stderr, "[DACDUMP] not monotonic at %d: %u %u %u after %u %u %u\n", i,
                            lut[i][0], lut[i][1], lut[i][2], lut[i - 1][0], lut[i - 1][1], lut[i - 1][2]);
            }
        fprintf(stderr, "[DACDUMP] %d breaks; R at 0,32,..,255:", bad);
        for (i = 0; i < 256; i += 32) fprintf(stderr, " %u", lut[i][0]);
        fprintf(stderr, " %u\n", lut[255][0]);
    }
}
/* dst = src through the palette (or a plain copy). */
void d3d8_posix_gamma_copy(uint32_t *dst, const uint32_t *src, size_t n)
{
    size_t i;
    if (!s_dac_active) { memcpy(dst, src, n * 4); return; }
    for (i = 0; i < n; i++) {
        uint32_t p = src[i];
        dst[i] = (p & 0xFF000000u) | s_dac_r[(p >> 16) & 0xFF] | s_dac_g[(p >> 8) & 0xFF] | s_dac_b[p & 0xFF];
    }
}
void d3d8_PassPhaseChanged(int old_phase, int new_phase, unsigned tag) { (void)old_phase; (void)new_phase; (void)tag; }
void d3d8_VeilSetCullFn(int (*fn)(float *out)) { (void)fn; }
int  d3d8_VeilWanted(void) { return 0; }

/* the picture extras: off */
int d3d8_bloom_wanted(void) { return 0; }
int d3d8_post_wanted(void) { return 0; }
int d3d8_post_split_wanted(void) { return 0; }
int d3d8_rscale_wanted(void) { return 0; }
int d3d8_temporal_wanted(void) { return 0; }
int d3d8_speedfx_wanted(void) { return 0; }
int d3d8_pump_alt_off(void) { return 0; }
void gpuprof_init(void) {}

/* ── display pacing: no GPU gate; the window loop is vsynced ─────────────── */
int  d3d8_sync_gate(void) { return 0; }
int  d3d8_sync_immediate(void) { return 0; }
int  d3d8_sync_queued(void) { return 0; }
long d3d8_sync_retired(void) { return 0; }
void d3d8_sync_set_frame_clock(void) {}
void d3d8_sync_set_pace(int fps) { (void)fps; }
void d3d8_sync_gate_stats(double *s, unsigned long long *resync) { if (s) *s = 0; if (resync) *resync = 0; }
/* The display's refresh rate (fps_cap.c: "Match screen" and the frames in
 * between above 60): the main display's mode (CoreGraphics), 120 on a
 * ProMotion screen; 60 when it says nothing. OT_DISPLAY_HZ overrides. */
static double display_hz(void)
{
    static double hz;
    if (!hz) {
        const char *e = getenv("OT_DISPLAY_HZ");
#if defined(__APPLE__)
        extern void *CGDisplayCopyDisplayMode(uint32_t display);
        extern uint32_t CGMainDisplayID(void);
        extern double CGDisplayModeGetRefreshRate(void *mode);
        extern void CGDisplayModeRelease(void *mode);
        void *m = CGDisplayCopyDisplayMode(CGMainDisplayID());
        hz = m ? CGDisplayModeGetRefreshRate(m) : 0.0;
        if (m) CGDisplayModeRelease(m);
#endif
        if (e && atof(e) >= 30.0) hz = atof(e);
        if (!(hz >= 30.0 && hz <= 500.0)) hz = 60.0;
        fprintf(stderr, "[D3D] display %.2f Hz\n", hz);
    }
    return hz;
}
double d3d8_display_rate(LONG *gen) { if (gen) *gen = 1; return display_hz(); }
int  d3d8_monitor_hz(HWND hwnd) { (void)hwnd; return (int)(display_hz() + 0.5); }
double d3d8_monitor_rate(HWND hwnd, const char **src) { (void)hwnd; if (src) *src = "CoreGraphics"; return display_hz(); }
void d3d8_test_display_hz_apply(void) {}
void d3d8_test_display_hz_restore(void) {}

/* ── screenshots (F12 on Windows): not yet on macOS ──────────────────────── */
void d3d8_RequestScreenshot(const wchar_t *path) { (void)path; }
int  d3d8_ScreenshotPending(void) { return 0; }
unsigned d3d8_ShotBurstCount(void) { return 1; }

/* The HD textures and the composed button atlases (nv2a_hdtex.c,
 * nv2a_btnicons.c) are made here as on Windows, from their decoded levels;
 * on macOS such a texture is level 0 kept in memory (BGRA8 rows), which the
 * Vulkan renderer uploads (vkr_backend.c, d3d8_posix_tex_level0) and then
 * releases. */
typedef struct { IDirect3DTexture8 base; uint32_t w, h; uint32_t *px; volatile int refs; } PosixTex;
/* Counted like a COM object: the button icons keep their composed atlas and
 * hand out a new reference at each upload (nv2a_btnicons.c). */
static ULONG __stdcall ptex_addref(IDirect3DTexture8 *t)
{
    return t ? (ULONG)__atomic_add_fetch(&((PosixTex *)t)->refs, 1, __ATOMIC_ACQ_REL) : 0;
}
static ULONG __stdcall ptex_release(IDirect3DTexture8 *t)
{
    PosixTex *p = (PosixTex *)t;
    int n;
    if (!p) return 0;
    n = __atomic_sub_fetch(&p->refs, 1, __ATOMIC_ACQ_REL);
    if (n <= 0) { free(p->px); free(p); }
    return (ULONG)(n > 0 ? n : 0);
}
static const IDirect3DTexture8Vtbl k_ptex_vtbl = { .AddRef = ptex_addref, .Release = ptex_release };

HRESULT d3d8_CreateTextureFromLevels(UINT Width, UINT Height, UINT Levels,
                                     const void *const *level_bits, IDirect3DTexture8 **ppTex)
{
    PosixTex *p;
    size_t n = (size_t)Width * Height * 4;
    if (ppTex) *ppTex = NULL;
    if (!ppTex || !Width || !Height || !Levels || !level_bits || !level_bits[0])
        return (HRESULT)0x8876086Cu;         /* D3DERR_INVALIDCALL */
    p = (PosixTex *)calloc(1, sizeof *p);
    if (!p || !(p->px = (uint32_t *)malloc(n))) { free(p); return (HRESULT)0x8007000Eu; }
    memcpy(p->px, level_bits[0], n);
    p->w = Width; p->h = Height;
    p->refs = 1;
    p->base.lpVtbl = &k_ptex_vtbl;
    *ppTex = &p->base;
    return 0;
}

void d3d8_posix_tex_release(IDirect3DTexture8 *t) { if (t) t->lpVtbl->Release(t); }

/* The HD loader's calls for the Vulkan backend, whose own vkr_hdtex.h
 * renames hdtex_* to its SSX 3 pack (vkr_backend.c). */
#include "../nv2a/nv2a_hdtex.h"
HdJob *ot_hdtex_request(const uint8_t *l0, uint32_t n0, uint32_t w, uint32_t h, uint32_t levels, int *gate)
{ return hdtex_request(l0, n0, w, h, levels, gate); }
int  ot_hdtex_collect(HdJob *job, IDirect3DTexture8 **tex, uint32_t *bytes) { return hdtex_collect(job, tex, bytes); }
void ot_hdtex_cancel(HdJob *job) { hdtex_cancel(job); }
int  ot_hdtex_on(void) { return hdtex_on(); }
int  ot_hdtex_gate_now(int kind) { return hdtex_gate_now(kind); }

int d3d8_posix_tex_level0(IDirect3DTexture8 *t, uint32_t *w, uint32_t *h, const uint32_t **px)
{
    const PosixTex *p = (const PosixTex *)t;
    if (!p || p->base.lpVtbl != &k_ptex_vtbl) return 0;
    *w = p->w; *h = p->h; *px = p->px;
    return 1;
}

/* The HLE D3D8 entry (the title uses its own statically linked D3D8). */
IDirect3D8 *xbox_Direct3DCreate8(UINT SDKVersion) { (void)SDKVersion; return NULL; }

/* ── exit ─────────────────────────────────────────────────────────────────── */
static void (*s_exit_hooks[4])(void);
void d3d8_SetHostExitHook(void (*fn)(void))
{
    int i;
    for (i = 0; i < 4; i++) if (!s_exit_hooks[i]) { s_exit_hooks[i] = fn; return; }
}
void d3d8_HostExitCode(unsigned code)
{
    int i;
    for (i = 0; i < 4; i++) if (s_exit_hooks[i]) s_exit_hooks[i]();
    fflush(NULL);
    _exit((int)code);
}
void d3d8_HostExit(void) { d3d8_HostExitCode(0); }
/* Direct3D 11's failure hook (d3d8_device.c): Vulkan reports its own
 * failures (vkr.c), so the hook is kept and never called. */
void d3d8_SetGpuFailHook(D3D8GpuFailFn fn) { (void)fn; }
/* The D3D11 shader prewarm (d3d8_shprewarm.c) is Windows-only: the Vulkan
 * renderer builds its pipelines from SPIR-V compiled with the game. */
void shpw_set_xbe(const void *xbe, size_t size) { (void)xbe; (void)size; }
void xbox_BeforeExit(void)
{
    int i;
    for (i = 0; i < 4; i++) if (s_exit_hooks[i]) s_exit_hooks[i]();
}
#endif /* !_WIN32 */
