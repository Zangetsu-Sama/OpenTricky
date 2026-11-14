/* SPDX-License-Identifier: GPL-3.0-only
 * hostui_posix.c -- hostui.h off Windows. The window's own keys (F11
 * fullscreen, F12 screenshot) belong to the SDL window loop here; nothing
 * to install on the game's side yet. */
#ifndef _WIN32
#include "win32_compat.h"
#include "../launcher.h"
#include "../hostui.h"
void hostui_install(LauncherConfig *cfg, BOOL persist_changes) { (void)cfg; (void)persist_changes; }
void hostui_test_ini(const char *ini) { (void)ini; }
#endif
