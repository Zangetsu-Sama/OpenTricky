/* SPDX-License-Identifier: GPL-3.0-only
 *
 * xinput_sdl.c -- XInputGetState / SetState / GetCapabilities on macOS,
 * from SDL3 gamepads (Xbox, PlayStation, Switch and other controllers the
 * GameController framework or SDL's HID drivers know), so controls.c reads
 * pads as it does on Windows. The window loop (fb_present_sdl3.c) runs
 * SDL's event pump on the main thread; this only reads the state.
 */
#ifndef _WIN32
#include <string.h>
#include <SDL3/SDL.h>
#include <xinput.h>

#define PADS 4
static SDL_Gamepad *s_pad[PADS];
static DWORD s_packet[PADS];

/* Slot i = the i-th connected gamepad, opened on first use. */
static SDL_Gamepad *pad(DWORD idx)
{
    SDL_JoystickID *ids;
    int n = 0;
    if (idx >= PADS || !SDL_WasInit(SDL_INIT_GAMEPAD)) return NULL;
    if (s_pad[idx] && SDL_GamepadConnected(s_pad[idx])) return s_pad[idx];
    if (s_pad[idx]) { SDL_CloseGamepad(s_pad[idx]); s_pad[idx] = NULL; }
    ids = SDL_GetGamepads(&n);
    if (ids && (int)idx < n) s_pad[idx] = SDL_OpenGamepad(ids[idx]);
    SDL_free(ids);
    return s_pad[idx];
}

static SHORT axis(SDL_Gamepad *g, SDL_GamepadAxis a, int invert)
{
    int v = SDL_GetGamepadAxis(g, a);
    if (invert) v = -v - 1;
    if (v < -32768) v = -32768;
    if (v > 32767) v = 32767;
    return (SHORT)v;
}

DWORD XInputGetState(DWORD idx, XINPUT_STATE *st)
{
    SDL_Gamepad *g = pad(idx);
    WORD b = 0;
    if (st) memset(st, 0, sizeof *st);
    if (!g || !st) return ERROR_DEVICE_NOT_CONNECTED;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP))        b |= XINPUT_GAMEPAD_DPAD_UP;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN))      b |= XINPUT_GAMEPAD_DPAD_DOWN;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT))      b |= XINPUT_GAMEPAD_DPAD_LEFT;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT))     b |= XINPUT_GAMEPAD_DPAD_RIGHT;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START))          b |= XINPUT_GAMEPAD_START;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_BACK))           b |= XINPUT_GAMEPAD_BACK;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_LEFT_STICK))     b |= XINPUT_GAMEPAD_LEFT_THUMB;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_RIGHT_STICK))    b |= XINPUT_GAMEPAD_RIGHT_THUMB;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))  b |= XINPUT_GAMEPAD_LEFT_SHOULDER;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) b |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_SOUTH))          b |= XINPUT_GAMEPAD_A;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_EAST))           b |= XINPUT_GAMEPAD_B;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_WEST))           b |= XINPUT_GAMEPAD_X;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_NORTH))          b |= XINPUT_GAMEPAD_Y;
    st->Gamepad.wButtons = b;
    st->Gamepad.bLeftTrigger  = (BYTE)(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) * 255 / 32767);
    st->Gamepad.bRightTrigger = (BYTE)(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) * 255 / 32767);
    st->Gamepad.sThumbLX = axis(g, SDL_GAMEPAD_AXIS_LEFTX, 0);
    st->Gamepad.sThumbLY = axis(g, SDL_GAMEPAD_AXIS_LEFTY, 1);    /* XInput: up is positive */
    st->Gamepad.sThumbRX = axis(g, SDL_GAMEPAD_AXIS_RIGHTX, 0);
    st->Gamepad.sThumbRY = axis(g, SDL_GAMEPAD_AXIS_RIGHTY, 1);
    st->dwPacketNumber = ++s_packet[idx];
    return ERROR_SUCCESS;
}

DWORD XInputSetState(DWORD idx, XINPUT_VIBRATION *vib)
{
    SDL_Gamepad *g = pad(idx);
    if (!g) return ERROR_DEVICE_NOT_CONNECTED;
    if (vib) SDL_RumbleGamepad(g, vib->wLeftMotorSpeed, vib->wRightMotorSpeed, 250);
    return ERROR_SUCCESS;
}

DWORD XInputGetCapabilities(DWORD idx, DWORD flags, XINPUT_CAPABILITIES *caps)
{
    (void)flags;
    if (!pad(idx)) return ERROR_DEVICE_NOT_CONNECTED;
    if (caps) {
        memset(caps, 0, sizeof *caps);
        caps->Type = XINPUT_DEVTYPE_GAMEPAD;
        caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
    }
    return ERROR_SUCCESS;
}
#endif /* !_WIN32 */
