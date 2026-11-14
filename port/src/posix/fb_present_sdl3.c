/* SPDX-License-Identifier: GPL-3.0-only
 *
 * fb_present_sdl3.c -- the game window on macOS, after SSX3-recomp's macOS
 * port (xboxrecomp/src/video/fb_present_sdl.c there), on SDL3.
 *
 * The Vulkan renderer draws offscreen and publishes each finished frame here
 * (xbox_FramebufferPublishImage, from its flip thread); frames the CPU wrote
 * into guest memory (the game's videos) come through
 * xbox_FramebufferWindowPresent. The window loop runs on the process's main
 * thread, as Cocoa wants (main.c starts the game on its own thread), and
 * uploads the newest frame to an SDL texture.
 *
 * It also owns the keyboard (GetAsyncKeyState for controls.c), the focus
 * (GetForegroundWindow) and the gamepads' event pump (xinput_sdl.c reads them).
 */
#ifndef _WIN32
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include "win32_compat.h"

extern ptrdiff_t xbox_GetMemoryOffset(void);

static volatile int s_running, s_focused;
static volatile int s_client_w, s_client_h;
static char s_title[64] = "SSX Tricky";

/* ── frames from the renderer: triple buffer (as SSX3-recomp) ───────────── */
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t *s_hb[3];
static int s_back = 0, s_mid = 1, s_front = 2;
static uint32_t s_w, s_h;
static uint32_t *s_old[3];
static int s_new;
static double s_at;
static volatile int s_flips;
int g_pub_src;   /* tests: 1 GPU frame, 2 video frame (who published last) */

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

void xbox_FramebufferApplyGamma(uint32_t *px, size_t n) { (void)px; (void)n; }
extern void d3d8_posix_gamma_copy(uint32_t *dst, const uint32_t *src, size_t n);
void xbox_FramebufferGammaCopy(uint32_t *dst, const uint32_t *src, size_t n)
{
    d3d8_posix_gamma_copy(dst, src, n);     /* the title's DAC palette (d3d8_posix.c) */
}

void xbox_FramebufferWindowStart(void);

/* OT_SHOT_DIR=<folder> (tests): the image shown, as a BMP, every 2 s. */
static void shot_maybe(const uint32_t *px, uint32_t w, uint32_t h)
{
    extern int g_pub_src;
    static const char *dir = (const char *)-1;
    static double last;
    static int n;
    double t;
    if (dir == (const char *)-1) dir = getenv("OT_SHOT_DIR");
    if (!dir || !*dir) return;
    t = now_ms();
    {
        static double every = -1;
        static double from = -1, t0;
        if (every < 0) { const char *e = getenv("OT_SHOT_MS"); every = e ? atof(e) : 2000.0; }
        if (from < 0) { const char *e = getenv("OT_SHOT_FROM_MS"); from = e ? atof(e) : 0.0; t0 = t; }
        if (t - t0 < from) return;              /* OT_SHOT_FROM_MS: none before then */
        if (t - last < every) return;
    }
    last = t;
    {
        char path[1024];
        FILE *f;
        uint32_t y, sz = w * h * 4;
        uint8_t hd[54] = { 'B', 'M' };
        snprintf(path, sizeof path, "%s/shot_%03d.bmp", dir, n++);
        f = fopen(path, "wb");
        if (!f) return;
        *(uint32_t *)(hd + 2) = 54 + sz; *(uint32_t *)(hd + 10) = 54;
        *(uint32_t *)(hd + 14) = 40; *(int32_t *)(hd + 18) = (int32_t)w; *(int32_t *)(hd + 22) = -(int32_t)h;
        *(uint16_t *)(hd + 26) = 1; *(uint16_t *)(hd + 28) = 32; *(uint32_t *)(hd + 34) = sz;
        fwrite(hd, 1, 54, f);
        for (y = 0; y < h; y++) fwrite(px + (size_t)y * w, 4, w, f);
        fclose(f);
        fprintf(stderr, "[SHOT] %s (%ux%u) src %d\n", path, w, h, g_pub_src);
    }
}

/* The share of the window's width the next published image is shown in,
 * centred (the 16:9 frame of the menus on a wider screen, pgraph_vk.c);
 * 1 = all of it. One per buffer, so it travels with its image. */
static volatile float s_box_next = 1.0f;
static float s_box_hb[3] = {1.0f, 1.0f, 1.0f};
void xbox_FramebufferSetBox(float k)
{
    k = k > 0.05f && k <= 1.0f ? k : 1.0f;
    if (k != s_box_next && getenv("OT_BOX_TRACE"))
        fprintf(stderr, "[BOX] images now shown at %.3f of the width\n", k);
    s_box_next = k;
}

/* F5..F9 in the game window (diagnostic): the OpenTricky extras switched off
 * live, one bit each -- read by the executor and the Vulkan renderer. */
volatile int ot_debug_off;

void xbox_FramebufferPublishImage(const uint32_t *px, uint32_t w, uint32_t h)
{
    float box = s_box_next;
    uint32_t *dst;
    int i;
    if (!px || !w || !h) return;
    xbox_FramebufferWindowStart();
    pthread_mutex_lock(&s_lock);
    if (w != s_w || h != s_h || !s_hb[0]) {
        for (i = 0; i < 3; i++) {
            free(s_old[i]);
            s_old[i] = s_hb[i];
            s_hb[i] = (uint32_t *)malloc((size_t)w * h * 4);
        }
        if (!s_hb[0] || !s_hb[1] || !s_hb[2]) {
            for (i = 0; i < 3; i++) { free(s_hb[i]); s_hb[i] = NULL; }
            s_w = s_h = 0;
            pthread_mutex_unlock(&s_lock);
            return;
        }
        s_w = w; s_h = h;
        s_new = 0;
    }
    dst = s_hb[s_back];
    pthread_mutex_unlock(&s_lock);
    xbox_FramebufferGammaCopy(dst, px, (size_t)w * h);
    shot_maybe(dst, w, h);                     /* as the window shows it (palette applied) */
    pthread_mutex_lock(&s_lock);
    if (s_hb[s_back] == dst) {
        int t;
        s_box_hb[s_back] = box;
        t = s_back; s_back = s_mid; s_mid = t;
        s_new = 1;
        s_at = now_ms();
    }
    pthread_mutex_unlock(&s_lock);
    __atomic_add_fetch(&s_flips, 1, __ATOMIC_RELAXED);
}

/* A frame in guest memory (A8R8G8B8 or R5G6B5, 640 wide), published as an
 * image: the title's own frames when the GPU drew nothing (pgraph_vk.c). */
static uint32_t *s_guest_px;
void xbox_FramebufferPublishGuest(uint32_t fb_va, uint32_t pitch)
{
    const uint32_t w = 640, h = 480;
    const uint8_t *src;
    uint32_t x, y, bpp;
    if (!fb_va || !pitch) return;
    if (!s_guest_px) s_guest_px = (uint32_t *)malloc((size_t)w * h * 4);
    if (!s_guest_px) return;
    bpp = pitch / w;
    src = (const uint8_t *)((uintptr_t)fb_va + xbox_GetMemoryOffset());
    for (y = 0; y < h; y++) {
        const uint8_t *row = src + (size_t)y * pitch;
        uint32_t *d = s_guest_px + (size_t)y * w;
        if (bpp >= 4) for (x = 0; x < w; x++) d[x] = ((const uint32_t *)row)[x] | 0xFF000000u;
        else for (x = 0; x < w; x++) {
            uint16_t v = ((const uint16_t *)row)[x];
            d[x] = 0xFF000000u | (((v >> 11) & 0x1F) * 255u / 31u) << 16 |
                   (((v >> 5) & 0x3F) * 255u / 63u) << 8 | ((v & 0x1F) * 255u / 31u);
        }
    }
    { extern int g_pub_src; g_pub_src = 3; }
    xbox_FramebufferPublishImage(s_guest_px, w, h);
}

/* The executor's own copy of a flipped surface (nv2a_pb_exec.c FLIP_STALL):
 * not shown. With the Vulkan renderer the GPU's image is the frame, published
 * by its flip thread; the guest surface gets it only afterwards, so showing it
 * here put a stale (black) image between every two real ones. */
void xbox_FramebufferWindowPresent(uint32_t fb_va, uint32_t pitch) { (void)fb_va; (void)pitch; }
void xbox_FramebufferGuestFrame(void) {}

void xbox_FramebufferWindowSet(uint32_t fb_va, uint32_t pitch) { (void)fb_va; (void)pitch; }
void xbox_FramebufferWindowSetTitle(const uint16_t *name, int max_chars)
{
    int i;
    for (i = 0; i < max_chars && i < 63 && name[i]; i++) s_title[i] = (char)name[i];
    if (i) s_title[i] = 0;
}
void xbox_FramebufferWindowFrameStats(uint32_t draws) { (void)draws; }
void xbox_FramebufferOsd(const char *text) { fprintf(stderr, "  [FBWIN] %s\n", text ? text : ""); }

int xbox_FramebufferClientSize(int *w, int *h)
{
    int cw = s_client_w, ch = s_client_h;
    if (cw <= 0 || ch <= 0) return 0;
    *w = cw; *h = ch;
    return 1;
}

/* ── keyboard: Windows virtual keys, from SDL scancodes ─────────────────── */
static volatile unsigned char s_key[256];

static int sc_to_vk(SDL_Scancode s)
{
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return 'A' + (s - SDL_SCANCODE_A);
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return '1' + (s - SDL_SCANCODE_1);
    if (s == SDL_SCANCODE_0) return '0';
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12) return VK_F1 + (s - SDL_SCANCODE_F1);
    if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_9) return VK_NUMPAD1 + (s - SDL_SCANCODE_KP_1);
    switch (s) {
    case SDL_SCANCODE_KP_0: return VK_NUMPAD0;
    case SDL_SCANCODE_KP_PERIOD: return VK_DECIMAL;
    case SDL_SCANCODE_KP_ENTER:
    case SDL_SCANCODE_RETURN: return VK_RETURN;
    case SDL_SCANCODE_ESCAPE: return VK_ESCAPE;
    case SDL_SCANCODE_SPACE: return VK_SPACE;
    case SDL_SCANCODE_TAB: return VK_TAB;
    case SDL_SCANCODE_BACKSPACE: return VK_BACK;
    case SDL_SCANCODE_LEFT: return VK_LEFT;
    case SDL_SCANCODE_UP: return VK_UP;
    case SDL_SCANCODE_RIGHT: return VK_RIGHT;
    case SDL_SCANCODE_DOWN: return VK_DOWN;
    case SDL_SCANCODE_LSHIFT: return VK_LSHIFT;
    case SDL_SCANCODE_RSHIFT: return VK_RSHIFT;
    case SDL_SCANCODE_LCTRL: return VK_LCONTROL;
    case SDL_SCANCODE_RCTRL: return VK_RCONTROL;
    case SDL_SCANCODE_LALT: return VK_LMENU;
    case SDL_SCANCODE_RALT: return VK_RMENU;
    case SDL_SCANCODE_COMMA: return VK_OEM_COMMA;
    case SDL_SCANCODE_PERIOD: return VK_OEM_PERIOD;
    case SDL_SCANCODE_MINUS: return VK_OEM_MINUS;
    case SDL_SCANCODE_EQUALS: return VK_OEM_PLUS;
    case SDL_SCANCODE_SEMICOLON: return VK_OEM_1;
    case SDL_SCANCODE_SLASH: return VK_OEM_2;
    case SDL_SCANCODE_GRAVE: return VK_OEM_3;
    case SDL_SCANCODE_LEFTBRACKET: return VK_OEM_4;
    case SDL_SCANCODE_BACKSLASH: return VK_OEM_5;
    case SDL_SCANCODE_RIGHTBRACKET: return VK_OEM_6;
    case SDL_SCANCODE_APOSTROPHE: return VK_OEM_7;
    case SDL_SCANCODE_DELETE: return VK_DELETE;
    case SDL_SCANCODE_HOME: return VK_HOME;
    case SDL_SCANCODE_END: return VK_END;
    case SDL_SCANCODE_PAGEUP: return VK_PRIOR;
    case SDL_SCANCODE_PAGEDOWN: return VK_NEXT;
    case SDL_SCANCODE_INSERT: return VK_INSERT;
    case SDL_SCANCODE_KP_MULTIPLY: return VK_MULTIPLY;
    case SDL_SCANCODE_KP_PLUS: return VK_ADD;
    case SDL_SCANCODE_KP_MINUS: return VK_SUBTRACT;
    case SDL_SCANCODE_KP_DIVIDE: return VK_DIVIDE;
    default: return 0;
    }
}

SHORT GetAsyncKeyState(int vk)
{
    int down;
    if (vk < 0 || vk > 255) return 0;
    switch (vk) {   /* the generic keys are either side */
    case VK_SHIFT:   down = s_key[VK_LSHIFT] || s_key[VK_RSHIFT]; break;
    case VK_CONTROL: down = s_key[VK_LCONTROL] || s_key[VK_RCONTROL]; break;
    case VK_MENU:    down = s_key[VK_LMENU] || s_key[VK_RMENU]; break;
    default:         down = s_key[vk];
    }
    return down ? (SHORT)0x8000 : 0;
}
SHORT GetKeyState(int vk) { return GetAsyncKeyState(vk); }

/* The game window (any non-NULL handle the game compares with its own,
 * d3d8_posix.c) while it has the keyboard. */
extern HWND d3d8_posix_window_handle(void);
HWND GetForegroundWindow(void) { return s_focused ? d3d8_posix_window_handle() : NULL; }

/* ── the window loop ──────────────────────────────────────────────────────── */
static void toggle_fullscreen(SDL_Window *win)
{
    int full = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(win, !full);
    fprintf(stderr, "[FBWIN] fullscreen %s\n", full ? "off" : "on");
}

__attribute__((weak)) void xbox_BeforeExit(void) {}

/* F12: the image shown, as a PNG, in <data folder>/Screenshots (the app's
 * "SSX Tricky data", OPENTRICKY_DATA_DIR) -- as hostui.c on Windows. Written
 * on a thread of its own so the window does not stutter. */
#include <sys/stat.h>
#include "miniz.h"
typedef struct { uint8_t *rgb; uint32_t w, h; char path[1024]; } Shot;
static void *shot_thread(void *arg)
{
    Shot *s = (Shot *)arg;
    size_t len = 0;
    void *png = tdefl_write_image_to_png_file_in_memory_ex(s->rgb, (int)s->w, (int)s->h, 3, &len, 6, 0);
    if (png) {
        FILE *f = fopen(s->path, "wb");
        if (f) { fwrite(png, 1, len, f); fclose(f); fprintf(stderr, "[SHOT] F12: %s\n", s->path); }
        mz_free(png);
    }
    free(s->rgb);
    free(s);
    return NULL;
}
static void take_screenshot(void)
{
    const char *dir = getenv("OPENTRICKY_DATA_DIR");
    char folder[900];
    Shot *s;
    uint32_t i, n;
    time_t t = time(NULL);
    struct tm tm;
    pthread_t th;
    if (!dir || !*dir) dir = ".";
    snprintf(folder, sizeof folder, "%s/Screenshots", dir);
    mkdir(folder, 0755);
    pthread_mutex_lock(&s_lock);
    if (!s_hb[0] || !s_w) { pthread_mutex_unlock(&s_lock); return; }
    s = (Shot *)calloc(1, sizeof *s);
    n = s_w * s_h;
    s->rgb = s ? (uint8_t *)malloc((size_t)n * 3) : NULL;
    if (!s || !s->rgb) { pthread_mutex_unlock(&s_lock); free(s); return; }
    s->w = s_w; s->h = s_h;
    for (i = 0; i < n; i++) {
        uint32_t p = s_hb[s_front][i];
        s->rgb[i * 3] = (uint8_t)(p >> 16); s->rgb[i * 3 + 1] = (uint8_t)(p >> 8); s->rgb[i * 3 + 2] = (uint8_t)p;
    }
    pthread_mutex_unlock(&s_lock);
    localtime_r(&t, &tm);
    snprintf(s->path, sizeof s->path, "%s/SSX Tricky %04d-%02d-%02d %02d-%02d-%02d.png", folder,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    if (pthread_create(&th, NULL, shot_thread, s) == 0) pthread_detach(th);
    else { free(s->rgb); free(s); }
}

static void window_loop(void)
{
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex = NULL;
    uint32_t tw = 0, th = 0;
    double t0 = now_ms();
    int f0 = 0;
    const char *res = getenv("XBOX_WINDOW_SIZE");
    int ww = 1280, wh = 960;

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "  [FBWIN] SDL video init failed: %s\n", SDL_GetError());
        s_running = 0;
        return;
    }
    if (res) sscanf(res, "%dx%d", &ww, &wh);
    win = SDL_CreateWindow(s_title, ww, wh, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    ren = win ? SDL_CreateRenderer(win, NULL) : NULL;
    if (!ren) {
        fprintf(stderr, "  [FBWIN] window setup failed: %s\n", SDL_GetError());
        s_running = 0;
        return;
    }
    SDL_SetRenderVSync(ren, 1);
    {
        const char *fs = getenv("XBOX_FULLSCREEN");
        if (fs && *fs == '1') toggle_fullscreen(win);
    }
    s_focused = 1;
    fprintf(stderr, "  [FBWIN] game window open (%dx%d points)\n", ww, wh);

    while (s_running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                xbox_BeforeExit();
                fflush(NULL);
                _exit(0);
            } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
                       (e.key.scancode == SDL_SCANCODE_F11 ||
                        (e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)) ||
                        (e.key.scancode == SDL_SCANCODE_F && (e.key.mod & SDL_KMOD_GUI) &&
                         (e.key.mod & SDL_KMOD_CTRL)))) {
                toggle_fullscreen(win);
            } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
                       e.key.scancode >= SDL_SCANCODE_F5 && e.key.scancode <= SDL_SCANCODE_F9) {
                /* diagnostic: F5 seams, F6 lightmap filter, F7 snow look, F8 soft
                 * shadows, F9 SMAA -- each switched off / on live (window title) */
                int bit = 1 << (e.key.scancode - SDL_SCANCODE_F5);
                __atomic_xor_fetch(&ot_debug_off, bit, __ATOMIC_RELAXED);
                fprintf(stderr, "[DEBUG-KEYS] off: %s%s%s%s%s\n",
                        ot_debug_off & 1 ? "seams " : "", ot_debug_off & 2 ? "lightmap " : "",
                        ot_debug_off & 4 ? "snow " : "", ot_debug_off & 8 ? "softshadows " : "",
                        ot_debug_off & 16 ? "smaa" : "");
            } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_F12) {
                take_screenshot();           /* not a key for the game */
            } else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
                int vk = sc_to_vk(e.key.scancode);
                if (vk) s_key[vk] = e.type == SDL_EVENT_KEY_DOWN;
            } else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                memset((void *)s_key, 0, sizeof s_key);
                s_focused = 0;
            } else if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                s_focused = 1;
            }
        }
        {
            int cw, ch;
            SDL_GetWindowSizeInPixels(win, &cw, &ch);
            s_client_w = cw; s_client_h = ch;
        }
        pthread_mutex_lock(&s_lock);
        if (s_hb[0] && s_at) {
            int fresh = 0;
            if (s_new) {
                int t = s_front; s_front = s_mid; s_mid = t;
                s_new = 0;
                fresh = 1;
            }
            if (s_w != tw || s_h != th) {
                if (tex) SDL_DestroyTexture(tex);
                tw = s_w; th = s_h;
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                        (int)tw, (int)th);
                if (tex) {
                    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                    /* The image is opaque, whatever its alpha holds (the seam
                     * blends leave 0.75 on the terrain's joints, the HUD less):
                     * shown as is, as D3D11 presents it. SDL3's default for a
                     * format with alpha blends it over the black clear --
                     * dark lines on every joint, a darker HUD. */
                    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
                }
                SDL_SetRenderLogicalPresentation(ren, (int)tw, (int)th,
                                                 SDL_LOGICAL_PRESENTATION_LETTERBOX);
                fresh = 1;
            }
            if (tex && fresh) SDL_UpdateTexture(tex, NULL, s_hb[s_front], (int)(tw * 4));
        }
        pthread_mutex_unlock(&s_lock);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        if (tex) {
            float k = s_box_hb[s_front];
            if (k < 0.999f) {
                SDL_FRect d = {(float)tw * (1.0f - k) * 0.5f, 0.0f, (float)tw * k, (float)th};
                SDL_RenderTexture(ren, tex, NULL, &d);
            } else {
                SDL_RenderTexture(ren, tex, NULL, NULL);
            }
        }
        SDL_RenderPresent(ren);
        {
            double now = now_ms();
            if (now - t0 >= 1000.0) {
                int f = s_flips;
                char tb[128];
                snprintf(tb, sizeof tb, "%s  |  %.0f FPS%s%s%s%s%s%s", s_title, (f - f0) * 1000.0 / (now - t0),
                         ot_debug_off ? "  |  OFF:" : "", ot_debug_off & 1 ? " seams(F5)" : "",
                         ot_debug_off & 2 ? " lightmap(F6)" : "", ot_debug_off & 4 ? " snow(F7)" : "",
                         ot_debug_off & 8 ? " soft-shadows(F8)" : "", ot_debug_off & 16 ? " SMAA(F9)" : "");
                SDL_SetWindowTitle(win, tb);
                {
                    static int log = -1;
                    if (log < 0) log = getenv("OT_FPS_LOG") != NULL;
                    if (log) fprintf(stderr, "[FPS] %.1f\n", (f - f0) * 1000.0 / (now - t0));
                }
                t0 = now; f0 = f;
            }
        }
    }
    if (tex) SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
}

/* macOS: the main thread waits here until the game asks for its window
 * (its first frame), then runs the window loop; main.c joins the game. */
static pthread_mutex_t s_main_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_main_cond = PTHREAD_COND_INITIALIZER;
static int s_main_want, s_main_quit;

void xbox_FramebufferMainThreadRun(void)
{
    int want;
    pthread_mutex_lock(&s_main_lock);
    while (!s_main_want && !s_main_quit)
        pthread_cond_wait(&s_main_cond, &s_main_lock);
    want = s_main_want;
    pthread_mutex_unlock(&s_main_lock);
    if (want) window_loop();
}

void xbox_FramebufferMainThreadQuit(void)
{
    pthread_mutex_lock(&s_main_lock);
    s_main_quit = 1;
    s_running = 0;
    pthread_cond_broadcast(&s_main_cond);
    pthread_mutex_unlock(&s_main_lock);
}

void xbox_FramebufferWindowStart(void)
{
    if (__sync_val_compare_and_swap(&s_running, 0, 1) != 0) return;
    pthread_mutex_lock(&s_main_lock);
    s_main_want = 1;
    pthread_cond_broadcast(&s_main_cond);
    pthread_mutex_unlock(&s_main_lock);
}
#endif /* !_WIN32 */
