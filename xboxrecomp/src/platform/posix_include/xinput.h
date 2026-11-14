/* POSIX stand-in for <xinput.h>: the XInput types and calls the port uses.
 * XINPUT_STATE / XINPUT_GAMEPAD / XInputGetState come from win32_compat.h;
 * the game links a real implementation over SDL3 (port/src/xinput_sdl.c). */
#pragma once
#include "win32_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#ifndef XUSER_MAX_COUNT
#define XUSER_MAX_COUNT 4
#endif
#define XINPUT_DEVTYPE_GAMEPAD        0x01
#define XINPUT_DEVSUBTYPE_GAMEPAD     0x01
#define XINPUT_DEVSUBTYPE_GC_GAMEPAD  0x01
#define XINPUT_FLAG_GAMEPAD           0x00000001
#define XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE  7849
#define XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE 8689
#define XINPUT_GAMEPAD_TRIGGER_THRESHOLD    30
typedef struct _XINPUT_VIBRATION {
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
} XINPUT_VIBRATION, *PXINPUT_VIBRATION;
typedef struct _XINPUT_CAPABILITIES {
    BYTE Type;
    BYTE SubType;
    WORD Flags;
    XINPUT_GAMEPAD Gamepad;
    XINPUT_VIBRATION Vibration;
} XINPUT_CAPABILITIES, *PXINPUT_CAPABILITIES;
DWORD XInputSetState(DWORD idx, XINPUT_VIBRATION *vib);
DWORD XInputGetCapabilities(DWORD idx, DWORD flags, XINPUT_CAPABILITIES *caps);
#ifdef __cplusplus
}
#endif
