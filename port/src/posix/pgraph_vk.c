/* SPDX-License-Identifier: GPL-3.0-only
 *
 * pgraph_vk.c -- the NV2A translator interface (nv2a_pgraph_d3d11.h) on
 * macOS: OpenTricky's push-buffer consumer (nv2a_live_pb.c) hands its NV2A
 * methods here, and they go to SSX3-recomp's executor (nv2a_exec/) and its
 * Vulkan renderer (src/vk, MoltenVK), the way SSX3-recomp's macOS port
 * draws.
 *
 * What this adds to the executor is what is SSX Tricky's own:
 *   - the frame boundary. The title never emits NV097_FLIP_STALL; it moves
 *     its render surface to the other buffer (SET_SURFACE_COLOR_OFFSET), as
 *     nv2a_pgraph_d3d11.c explains. A change of surface after draws is the
 *     executor's FLIP_STALL here, so it presents the buffer just finished;
 *   - the fork's NOP markers (pumplag.c's draw fence), which the game side
 *     waits on;
 *   - the texture registers a substitution filter asks about.
 * The fork's picture extras (HUD anchoring, terrain substitution, lightmap
 * filter...) are not wired to the Vulkan renderer yet: their setters are
 * kept and do nothing.
 */
#ifndef _WIN32
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32_compat.h"
#include "nv2a/nv2a_pgraph_d3d11.h"

extern void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param);
extern int vkr_backend_install(void);
extern void xbox_FramebufferPublishGuest(uint32_t fb_va, uint32_t pitch);

#define NV097_NO_OPERATION            0x0100
#define NV097_FLIP_STALL              0x0130
#define NV097_SET_SURFACE_PITCH       0x020C
#define NV097_SET_SURFACE_COLOR_OFFSET 0x0210
#define NV097_SET_BEGIN_END           0x17FC
#define NV097_SET_TEXTURE_OFFSET      0x1B00
#define NV097_SET_TEXTURE_FORMAT      0x1B04
#define NV097_SET_TEXTURE_ADDRESS     0x1B08
#define NV097_SET_TEXTURE_CONTROL0    0x1B0C
#define NV097_SET_TEXTURE_FILTER      0x1B14

static struct {
    int initialized;
    int vk;                         /* the Vulkan renderer is drawing */
    uint32_t surface;               /* SET_SURFACE_COLOR_OFFSET */
    int surface_seen;
    uint32_t pitch;
    unsigned draws, draws_since_flip, frames, methods;
    int flip_mode;
    uint64_t last_flip_ms;
    int frame_complete;
    uint32_t tex_offset[4], tex_format[4], tex_address[4], tex_control0[4], tex_filter[4];
} g;

volatile uint32_t pgraph_d3d11_fork_fence;

void pgraph_d3d11_init(void)
{
    if (g.initialized) return;
    g.initialized = 1;
    /* On the pump thread, like the D3D11 device on Windows: every draw is
     * recorded by this one thread. */
    /* OT_NO_VK=1 (tests): the executor's own software rasteriser draws into
     * guest memory, shown from there -- a reference for the Vulkan path. */
    g.vk = getenv("OT_NO_VK") ? 0 : vkr_backend_install();
    {   /* the button atlases, composed ahead of use (as nv2a_live_pb.c on Windows) */
        extern void btnicons_prepare(void);
        if (g.vk) btnicons_prepare();
    }
    fprintf(stderr, "[PGRAPH] macOS: NV2A executor + %s\n",
            g.vk ? "Vulkan renderer (MoltenVK)" : "software rasteriser (no Vulkan)");
}

void pgraph_d3d11_shutdown(void) {}

/* ── pass phases and the 2D tags (as nv2a_pgraph_d3d11.c) ───────────────
 * The title's hooks (port/src/pass_tags.c, hud_anchor.c) write NOP markers
 * into the push buffer: the pass phase of the draws that follow, and the
 * race HUD's elements (anchor and scale), the 16:9 frame's records and the
 * panels' edges. hud_element below is the D3D11 renderer's: the Vulkan
 * backend asks it at each 2D draw (vkr_backend.c, ot_hud_element) and moves
 * the draw's vertices, on the CPU. Pump thread only. */
#define NV2A_PASS_TAG_MAGIC 0x5358u

static struct {
    float kx, ky;
    int   on;
    float ax, ay;
    unsigned long long tags, draws;
} g_hud;

static struct {
    float k, k2d, kboxed, k169;
    int   on, img_wide, next_wide, img_169, next_169, tagged;
} g_box;

static struct {
    int   on, wide_only, scale_only;
    float l, r;
} g_panel;

static struct {
    int phase, in_frame, group;
} g_ph;

static pgraph_pass_phase_fn s_phase_cb;
void pgraph_d3d11_set_pass_phase_callback(pgraph_pass_phase_fn fn) { s_phase_cb = fn; }
int  pgraph_d3d11_pass_phase(void) { return g_ph.phase; }
static pgraph_image_end_fn s_image_end_cb;
void pgraph_d3d11_set_image_end_callback(pgraph_image_end_fn fn) { s_image_end_cb = fn; }

void pgraph_d3d11_set_hud_scale(float kx, float ky)
{
    int on = kx > 0.0f && ky > 0.0f && (kx < 1.0f || ky < 1.0f);
    g_hud.kx = on ? kx : 0.0f;
    g_hud.ky = on ? ky : 0.0f;
}
void pgraph_d3d11_set_box(float k) { g_box.k = k > 0.0f && k < 1.0f ? k : 0.0f; }
void pgraph_d3d11_set_box_race2d(float k2d) { g_box.k2d = k2d > 0.0f && k2d < 1.0f ? k2d : 0.0f; }
void pgraph_d3d11_set_box_frame(float kboxed) { g_box.kboxed = kboxed > 0.0f ? kboxed : 0.75f; }
void pgraph_d3d11_set_box169(float k169) { g_box.k169 = k169 > 0.0f && k169 < 1.0f ? k169 : 0.0f; }

static void phase_set(int ph, uint32_t tag)
{
    if (ph != g_ph.phase) {
        int old = g_ph.phase;
        g_ph.phase = ph;
        if (s_phase_cb) s_phase_cb(old, ph, tag);
    }
}

static void pass_tag_seen(uint32_t p)
{
    g_hud.on = g_box.on = g_panel.on = 0;      /* a 2D tag holds until the next pass tag */
    switch ((p >> 14) & 3u) {
    case 0:                                     /* FRAME_BEGIN */
        g_ph.in_frame = 1;
        g_ph.group = -1;
        phase_set(PGRAPH_PHASE_BEGIN, p);
        break;
    case 1:                                     /* FRAME_END */
        g_ph.in_frame = 0;
        g_ph.group = -1;
        phase_set(PGRAPH_PHASE_END, p);
        if (g.vk) { extern void ot_vk_frame_end(void); ot_vk_frame_end(); }
        break;
    case 2: {                                   /* GROUP */
        uint32_t grp = (p >> 5) & 31u, ortho = (p >> 13) & 1u;
        g_ph.group = (int)grp;
        phase_set(grp < 7u ? PGRAPH_PHASE_3D : ortho ? PGRAPH_PHASE_HUD : PGRAPH_PHASE_AFTER3D_PERSP, p);
        break;
    }
    default:
        break;
    }
}

static void hud_tag_seen(uint32_t p)
{
    uint32_t m = p >> 16;
    g_hud.tags++;
    if (m == PGRAPH_PANEL_TAG_MAGIC_R) { g_panel.r = (float)(p & 0x3FFFu) / 16.0f; return; }
    if (m == PGRAPH_PANEL_TAG_MAGIC) {
        g_hud.on = g_box.on = 0;
        g_panel.on = 1;
        g_panel.wide_only = (p & 0x8000u) != 0;
        g_panel.scale_only = (p & 0x4000u) != 0;
        g_panel.l = (float)(p & 0x3FFFu) / 16.0f;
        g_panel.r = 1e9f;
        return;
    }
    g_panel.on = 0;
    if (m == PGRAPH_BOX_TAG_MAGIC) {
        switch (p & 7u) {
        case 0: case 1:
            g_box.on = (int)(p & 1u);
            g_hud.on = 0;
            break;
        default:
            g_box.img_wide = g_box.next_wide;
            g_box.img_169 = g_box.next_169;
            g_box.next_169 = (p & 7u) == 4u && g_box.k169 > 0.0f;
            g_box.next_wide = (p & 7u) == 3u || g_box.next_169;
            g_box.tagged = 1;
            break;
        }
        return;
    }
    g_box.on = 0;
    if (m == PGRAPH_HUD_TAG_MAGIC) {
        g_hud.on = (p & 0x8000u) != 0;
        g_hud.ax = (float)(p & 0x7FFFu) / 16.0f;
        g_hud.ay = 240.0f;
    } else {
        g_hud.ay = (float)(p & 0x7FFFu) / 16.0f;
    }
}

/* The frame the image just finished is shown in: 1 = the whole width, else
 * the share of it the centred frame takes (16:9 menus on a wider screen).
 * Read by the Vulkan flip (vkr_backend.c) for the image it publishes. */
static volatile float s_present_box = 1.0f;
float ot_present_box(void) { return s_present_box; }

static void image_done(void)
{
    if (g_box.k > 0.0f) {
        int boxed = !(g_box.tagged && g_box.img_wide);
        int w169 = !boxed && g_box.img_169;
        g_box.tagged = 0;
        s_present_box = boxed ? g_box.k : w169 ? g_box.k169 : 1.0f;
    } else {
        s_present_box = 1.0f;
    }
    g_hud.on = g_box.on = g_panel.on = 0;      /* XBOX_FIX_HUD_3D: the 2D tags end with the image */
    if (s_image_end_cb) s_image_end_cb();
}

/* The scale of the current HUD element, in the guest surface's pixels (sw x
 * sh); 0 when the draw is not part of one. kx < 0: a panel's end pieces,
 * ky and ay then its left and right edges. As nv2a_pgraph_d3d11.c. */
int ot_hud_element(float sw, float sh, float *kx, float *ax, float *ky, float *ay)
{
    int wide = g_box.k > 0.0f ? g_box.img_wide : g_box.next_wide;
    float k169 = g_box.k > 0.0f && wide && g_box.img_169 && g_box.k169 > 0.0f ? g_box.k169 : 1.0f;
    if (g_ph.phase != PGRAPH_PHASE_HUD) return 0;
    if (g_panel.on) {
        int boxed = g_box.k > 0.0f && !wide;
        float k = boxed ? (g_box.kboxed > 0.0f ? g_box.kboxed : 0.75f) : g_box.k2d / k169;
        if (g_panel.wide_only && !wide) return 0;
        if (!(k > 0.0f && k < 1.0f)) {
            if (g_panel.scale_only || (!boxed && (g_box.k > 0.0f || g_box.k2d > 0.0f))) return 0;
            k = 1.0f;
        }
        *ax = 0.5f * sw;
        if (g_panel.scale_only) { *kx = k; *ky = 1.0f; *ay = 0.0f; }
        else { *kx = -k; *ky = g_panel.l * sw / 640.0f; *ay = g_panel.r * sw / 640.0f; }
        g_hud.draws++;
        return 1;
    }
    if (g_box.on && wide && (g_box.k2d > 0.0f || g_box.k > 0.0f)) {
        *kx = (g_box.k2d > 0.0f ? g_box.k2d : g_box.k) / k169;
        *ky = 1.0f;
        *ax = 0.5f * sw;
        *ay = 0.0f;
        g_hud.draws++;
        return 1;
    }
    if (!(g_hud.on && g_hud.kx > 0.0f)) return 0;
    *kx = g_hud.kx / k169;
    *ky = g_hud.ky;
    *ax = g_hud.ax * sw / 640.0f;
    *ay = g_hud.ay * sh / 480.0f;
    g_hud.draws++;
    return 1;
}

/* Any 2D tag in force (the backend leaves such draws to the CPU). */
int ot_hud_maybe(void)
{
    return g_ph.phase == PGRAPH_PHASE_HUD && (g_hud.on || g_box.on || g_panel.on);
}

extern void hdtex_frame_gate(unsigned bits) __attribute__((weak));

static void nop_note(uint32_t param)
{
    uint32_t m = param >> 16;
    if ((param >> 24) == 0x5Cu) {               /* fork draw fence (pumplag.c) */
        pgraph_d3d11_fork_fence = param & 0xFFFFFFu;
        return;
    }
    if (m == NV2A_PASS_TAG_MAGIC) { pass_tag_seen(param); return; }
    if (m == PGRAPH_HUD_TAG_MAGIC || m == PGRAPH_HUD_TAG_MAGIC_Y || m == PGRAPH_BOX_TAG_MAGIC ||
        m == PGRAPH_PANEL_TAG_MAGIC || m == PGRAPH_PANEL_TAG_MAGIC_R) { hud_tag_seen(param); return; }
    if (m == PGRAPH_HDGATE_TAG_MAGIC) { if (hdtex_frame_gate) hdtex_frame_gate(param & 3u); return; }
}

extern int d3d8_posix_video_flip(uint32_t offset);
extern void d3d8_posix_note_present(void);

static void frame_boundary(void)
{
    static int tr = -1;
    if (tr < 0) tr = getenv("OT_FLIP_TRACE") ? 400 : 0;
    if (tr > 0) { tr--; fprintf(stderr, "[FLIPT] leave %08X draws %u\n", g.surface, g.draws_since_flip); }
    /* A video frame (the CPU wrote it, the GPU only drew around it): shown
     * from guest memory, as d3d8_GuestFramebufferFlip on Windows. */
    static unsigned nvid, ngpu, nnone, nb;
    if (getenv("OT_FLIP_STATS") && ++nb % 120 == 0)
        fprintf(stderr, "[FLIPSTAT] video %u gpu %u none %u (draws this frame %u)\n", nvid, ngpu, nnone, g.draws_since_flip);
    image_done();
    if (d3d8_posix_video_flip(g.surface)) {
        nvid++;
        g.frames++;
    } else if (g.draws_since_flip) {
        ngpu++;
        nv2a_pb_exec_method(0, NV097_FLIP_STALL, 0);
        if (!g.vk) xbox_FramebufferPublishGuest(g.surface, g.pitch ? g.pitch : 2560);
        d3d8_posix_note_present();
        g.frames++;
    } else nnone++;
    g.draws_since_flip = 0;
    g.frame_complete = 1;
    g.flip_mode = 1;
    g.last_flip_ms = GetTickCount64();
    {   /* OT_EXEC_REPORT=N (tests): the executor's report every N frames */
        static int every = -1;
        extern void nv2a_pb_exec_report(void);
        if (every < 0) { const char *e = getenv("OT_EXEC_REPORT"); every = e ? atoi(e) : 0; }
        if (every > 0 && g.frames % (unsigned)every == 0) nv2a_pb_exec_report();
    }
}

int pgraph_d3d11_method(int subchannel, uint32_t method, uint32_t param)
{
    if (!g.initialized) return 0;
    g.methods++;
    if (subchannel == 0) {
        switch (method) {
        case NV097_NO_OPERATION:
            nop_note(param);
            break;
        case NV097_SET_SURFACE_PITCH:
            g.pitch = param & 0xFFFFu;
            break;
        case NV097_SET_SURFACE_COLOR_OFFSET:
            if (g.surface_seen && param != g.surface) frame_boundary();
            g.surface = param;
            g.surface_seen = 1;
            break;
        case NV097_FLIP_STALL:
            /* The title's own: the frame boundary is its surface flip
             * (frame_boundary), as on Windows; passed on as well, every
             * frame was presented twice, the second time empty (black). */
            return 1;
        case NV097_SET_BEGIN_END:
            if (param) { g.draws++; g.draws_since_flip++; }
            break;
        default:
            if (method >= NV097_SET_TEXTURE_OFFSET && method < NV097_SET_TEXTURE_OFFSET + 4 * 0x40) {
                int st = (int)((method - NV097_SET_TEXTURE_OFFSET) / 0x40);
                switch ((method - NV097_SET_TEXTURE_OFFSET) % 0x40) {
                case 0x00: g.tex_offset[st] = param; break;
                case 0x04: g.tex_format[st] = param; break;
                case 0x08: g.tex_address[st] = param; break;
                case 0x0C: g.tex_control0[st] = param; break;
                case 0x14: g.tex_filter[st] = param; break;
                }
            }
            break;
        }
    }
    nv2a_pb_exec_method((uint32_t)subchannel, method, param);
    return 1;
}

/* Runs: word by word (the executor takes methods one at a time). */
int pgraph_d3d11_index16_run(int subchannel, const uint32_t *w, uint32_t n)
{ (void)subchannel; (void)w; (void)n; return 0; }
uint32_t pgraph_d3d11_xfconst_run(int subchannel, uint32_t method, const uint32_t *w, uint32_t n)
{ (void)subchannel; (void)method; (void)w; (void)n; return 0; }
uint32_t pgraph_d3d11_run(int subchannel, uint32_t method, const uint32_t *w, uint32_t n)
{ (void)subchannel; (void)method; (void)w; (void)n; return 0; }

unsigned pgraph_d3d11_frame_count(void) { return g.frames; }
void pgraph_d3d11_flush(void) {}
void pgraph_d3d11_set_chyron_scroll(uint32_t frame) { (void)frame; }
void pgraph_d3d11_set_mem_base(void *base, uint32_t size) { (void)base; (void)size; }
/* The draw substitution filter (terrain_seams.c) and the draw observer:
 * the executor runs them at each program draw (nv2a_pb_exec.c ot_subst_draw). */
extern void nv2a_exec_set_draw_subst(pgraph_draw_subst_fn fn);
extern void nv2a_exec_set_draw_observe(pgraph_draw_observe_fn fn);
void pgraph_d3d11_set_draw_subst(pgraph_draw_subst_fn fn) { nv2a_exec_set_draw_subst(fn); }
void pgraph_d3d11_set_draw_observe(pgraph_draw_observe_fn fn) { nv2a_exec_set_draw_observe(fn); }

void pgraph_d3d11_tex_binding(int stage, uint32_t *offset, uint32_t *format)
{
    if (stage < 0 || stage > 3) { *offset = *format = 0; return; }
    *offset = g.tex_offset[stage];
    *format = g.tex_format[stage];
}
uint32_t pgraph_d3d11_tex_address(int stage)
{ return (stage >= 0 && stage < 4) ? g.tex_address[stage] : 0; }
void pgraph_d3d11_tex_state(int stage, uint32_t *format, uint32_t *filter, uint32_t *address,
                            uint32_t *control0, int *enabled)
{
    if (stage < 0 || stage > 3) stage = 0;
    if (format) *format = g.tex_format[stage];
    if (filter) *filter = g.tex_filter[stage];
    if (address) *address = g.tex_address[stage];
    if (control0) *control0 = g.tex_control0[stage];
    if (enabled) *enabled = (g.tex_control0[stage] >> 30) & 1;
}

void pgraph_d3d11_present(uint32_t crtc_start) { (void)crtc_start; }
int  pgraph_d3d11_take_frame_complete(void) { int f = g.frame_complete; g.frame_complete = 0; return f; }
/* As nv2a_pgraph_d3d11.c: flips seen, the last one under 250 ms ago. When
 * it stops (a loading screen drawn once, a still the title leaves up), the
 * pump presents what was drawn so far (nv2a_live_pb.c, d3d8_PresentFrame). */
int  pgraph_d3d11_flipping(void)
{
    return g.flip_mode && GetTickCount64() - g.last_flip_ms < 250;
}

/* d3d8_PresentFrame on macOS: the image drawn since the last flip, shown
 * without a flip (the pump's fallback present). */
void pgraph_vk_present_now(void)
{
    extern void d3d8_posix_note_present(void);
    if (!g.initialized) return;
    if (g.draws_since_flip) {
        nv2a_pb_exec_method(0, NV097_FLIP_STALL, 0);
        g.draws_since_flip = 0;
        g.frames++;
    }
    d3d8_posix_note_present();
}
void pgraph_d3d11_test_quad(void) {}

void pgraph_d3d11_get_stats(PgraphD3D11Stats *out)
{
    memset(out, 0, sizeof *out);
    out->frames = g.frames;
    out->draw_calls = g.draws;
    out->methods_handled = g.methods;
}

void pgraph_d3d11_report_nops(void) {}
/* OpenTricky: the visibility tests' reports, written once the GPU has run
 * them (nv2a_pb_exec.c), as nv2a_pgraph_d3d11.c */
void pgraph_d3d11_poll_reports(void) { extern void nv2a_exec_poll_reports(void); nv2a_exec_poll_reports(); }
void pgraph_d3d11_report_occlusion(void) {}
void pgraph_d3d11_report_ignored(void) {}
int  pgraph_d3d11_reports_pending(void) { extern int nv2a_exec_reports_pending(void); return nv2a_exec_reports_pending(); }
unsigned pgraph_d3d11_draws_pending(void) { return 0; }
uint32_t pgraph_d3d11_draw_count(void) { return g.draws; }
void pgraph_d3d11_tick_begin(void) {}
void pgraph_d3d11_perf_counts(unsigned *draws, unsigned *methods, unsigned *verts)
{
    if (draws) *draws = g.draws;
    if (methods) *methods = g.methods;
    if (verts) *verts = 0;
}

typedef int (*pgraph_plag_fn)(uint32_t q, uint32_t *cb, const uint8_t **snap);
void pgraph_d3d11_set_plag_check(pgraph_plag_fn fn) { (void)fn; }

static int s_lightmap;
int  pgraph_d3d11_set_lightmap_filter(int mode) { s_lightmap = mode; return 1; }
int  pgraph_d3d11_lightmap_filter(void) { return s_lightmap; }
void pgraph_d3d11_set_lines_scale(float k) { (void)k; }
void pgraph_d3d11_set_softcut(int on) { (void)on; }
void pgraph_d3d11_set_zdecal(int on) { (void)on; }
void pgraph_d3d11_set_ztie(int on) { (void)on; }

void pgraph_d3d11_present_guest_fb(uint32_t va, uint32_t pitch, uint32_t w, uint32_t h)
{
    (void)w; (void)h;
    xbox_FramebufferPublishGuest(va, pitch);
}

/* The executor's flip: the title's own swap counters (OpenTricky's pump
 * keeps the frame counters itself). */
void xbox_Nv2aFrameCounterFlip(void) {}
#endif /* !_WIN32 */
