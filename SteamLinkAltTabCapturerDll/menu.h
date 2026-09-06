#pragma once

#include "pch.h"

struct SDL_Window;

#define MENU_TOGGLE_GRAB  0x9000
#define HOTKEY_ID_GRAB    1

// Initialize the subclass and grab context for a window.
// pWin:      SDL_Window* to grab/ungrab.
// pGrabbed:  pointer to the grabbed state flag (owned by caller).
void Menu_InitSubclass(HWND hwnd, SDL_Window* pWin, BOOL* pGrabbed);

// Remove the subclass and unregister the hotkey.
void Menu_CleanupSubclass(HWND hwnd);
