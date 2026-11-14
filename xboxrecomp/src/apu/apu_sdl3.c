/* SPDX-License-Identifier: GPL-3.0-only
 *
 * apu_sdl3.c -- the xa2_* output (apu_xaudio2.h) on SDL3 audio, for macOS.
 *
 * Same contract as the XAudio2 backend: 48 kHz, interleaved 16-bit, stereo
 * (or 5.1 with XBOX_AUDIO_OUTPUT=5.1), submitted by the APU frame thread in
 * 256-sample buffers. The frame thread paces itself on xa2_queued(), the
 * buffers the device has yet to play: here the bytes queued in the SDL audio
 * stream, counted in those buffers.
 */
#if defined(XBOX_APU_SDL3) && !defined(_WIN32)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "apu_xaudio2.h"

#define XA2_SAMPLE_RATE  48000
#define XA2_BUF_SAMPLES  256
#define XA2_NUM_BUFS     24

static SDL_AudioStream *g_stream;
static int g_channels = 2;
static int g_active;
static unsigned long g_drops;

int xa2_target_queued(void)
{
    static int target = 0;
    if (!target) {
        const char *e = getenv("XBOX_AUDIO_QUEUE");
        int n = (e && *e) ? atoi(e) : XA2_TARGET_QUEUED;
        if (n < XA2_QUEUE_MIN) n = XA2_QUEUE_MIN;
        if (n > XA2_QUEUE_MAX) n = XA2_QUEUE_MAX;
        target = n;
        fprintf(stderr, "[XA2] queue target %d buffers (~%.1f ms)%s\n", target,
                target * XA2_BUF_SAMPLES * 1000.0 / XA2_SAMPLE_RATE,
                (e && *e) ? " from XBOX_AUDIO_QUEUE" : "");
    }
    return target;
}

int xa2_init(void)
{
    SDL_AudioSpec spec;
    const char *out = getenv("XBOX_AUDIO_OUTPUT");

    if (g_active) return 1;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "[XA2] SDL audio unavailable: %s\n", SDL_GetError());
        return 0;
    }
    g_channels = (out && (strcmp(out, "5.1") == 0 || strcmp(out, "51") == 0)) ? 6 : 2;
    spec.format = SDL_AUDIO_S16;
    spec.channels = g_channels;
    spec.freq = XA2_SAMPLE_RATE;
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!g_stream && g_channels == 6) {
        g_channels = spec.channels = 2;
        g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    }
    if (!g_stream) {
        fprintf(stderr, "[XA2] could not open the audio device: %s\n", SDL_GetError());
        return 0;
    }
    {   /* OT_MUTE=1 (tests): silent, the same pacing */
        const char *m = getenv("OT_MUTE");
        if (m && m[0] == '1') SDL_SetAudioStreamGain(g_stream, 0.0f);
    }
    SDL_ResumeAudioStreamDevice(g_stream);
    g_active = 1;
    fprintf(stderr, "[XA2] SDL3 audio: %d Hz, %d channels\n", XA2_SAMPLE_RATE, g_channels);
    return 1;
}

void xa2_shutdown(void)
{
    if (g_stream) SDL_DestroyAudioStream(g_stream);
    g_stream = NULL;
    g_active = 0;
}

int xa2_is_active(void) { return g_active; }
int xa2_channels(void) { return g_channels; }
int xa2_get_buffer_size(void) { return XA2_BUF_SAMPLES; }

int xa2_queued(void)
{
    int bytes;
    if (!g_stream) return 0;
    bytes = SDL_GetAudioStreamQueued(g_stream);
    if (bytes < 0) return 0;
    return bytes / (XA2_BUF_SAMPLES * g_channels * (int)sizeof(int16_t));
}

int xa2_submit_samples(const int16_t *samples, int num_samples)
{
    if (!g_stream || num_samples <= 0) return 0;
    if (xa2_queued() >= XA2_NUM_BUFS) { g_drops++; return 0; }
    if (num_samples > XA2_BUF_SAMPLES) num_samples = XA2_BUF_SAMPLES;
    return SDL_PutAudioStreamData(g_stream, samples,
                                  num_samples * g_channels * (int)sizeof(int16_t)) ? 1 : 0;
}
#endif
