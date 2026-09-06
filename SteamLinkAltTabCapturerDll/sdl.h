#pragma once

#include "pch.h"

// Find the first visible window with class "SDL_app" in the current process.
// Uses FindWindowExA recursion (3 levels) because EnumWindows hangs
// with Steam Link's window hierarchy.
HWND FindSDLWindow(void);

// Initialize SDL3 function pointers from the loaded SDL3.dll.
// Returns TRUE if all required functions are found.
BOOL SDL3_Init(void);

// Get SDL_Window* from an HWND by reading the SDL_WindowData property.
// Returns NULL if the property is missing or invalid.
// SDL3 struct layout (SDL_windowswindow.h):
//   +0x00  SDL_Window *window
//   +0x04  HWND hwnd
struct SDL_Window;
SDL_Window* SDL3_GetWindow(HWND hwnd);

// Enable or disable SDL3 keyboard grab on the given window.
// When enabled, SDL3 installs a WH_KEYBOARD_LL hook that intercepts
// modifier keys (Alt, Ctrl, Win) and system keys (Tab, Esc, PrtScn).
BOOL SDL3_SetKeyboardGrab(SDL_Window* window, BOOL enable);

// Query current keyboard grab state.
BOOL SDL3_GetKeyboardGrab(SDL_Window* window);

// Set an SDL3 hint.  Returns TRUE on success.
BOOL SDL3_SetHint(const char* name, const char* value);
