# SteamLink Alt+Tab Capturer — Reverse Engineering Summary

## Project Goal

Capture system keyboard shortcuts (Alt+Tab, Win+D, Ctrl+Esc, etc.) on the host and forward them to the remote streaming machine via Steam Link, using DLL injection and binary patching.

---

## 1. Binary Overview

- **Target**: `F:\Steam Link\SteamLink.exe` — 32-bit PE, 15.7 MB
- **Image Base**: `0x400000`, Image Size: `0xF0E000`
- **Dependencies**: SDL3.dll, Qt5*.dll, steamwebrtc.dll
- **IDA Database**: `reverse_engineer/SteamLink.exe.i64`
- **Stats**: 17,848 functions, 86,400 strings

## 2. Architecture

```
SteamLink.exe
├─ Qt5 UI Layer (main window, class "Qt5QWindowIcon")
│    └─ Menu, settings, connection UI
└─ SDL3 Streaming Window (class "SDL_app", created on stream start)
     ├─ Rendering (OpenGL/D3D)
     ├─ Input capture (keyboard, mouse, gamepad)
     └─ Streaming protocol (protobuf over UDP/TCP)
```

**Key insight**: The Qt GUI appears first. The SDL_app window is only created when streaming starts, and destroyed when streaming stops. The DLL must handle this lifecycle.

## 3. Key Functions (IDA addresses, base `0x400000`)

| Address | Name | Purpose |
|---------|------|---------|
| `0x5A4E20` | `sub_5A4E20` | Main SDL event handler (88-case switch + nested 254-case switch) |
| `0x5A5270` | `sub_5A5270` | SDL_KEYDOWN/SDL_KEYUP entry (event types 768/769) |
| `0x5B25F0` | `sub_5B25F0` | Key processing: scancode→keycode via `SDL_GetKeyFromScancode` |
| `0x59C620` | `sub_59C620` | Sends `CInputKeyDownMsg`/`CInputKeyUpMsg` to remote (message type 58) |
| `0x5B4DC0` | `sub_5B4DC0` | Alt+Enter handler (fullscreen toggle) |
| `0x5B2C30` | `sub_5B2C30` | Alt+F4 handler (close/quit) |

## 4. Keyboard Event Flow

```
Physical key press
  → Windows input system
  → SDL3 event queue (SDL_PumpEvents)
  → sub_5A4E20 (main event handler, event types 768/769)
     → Check: is this Tab with Alt held? (0x5A532C-0x5A533D)
        → YES: SKIP (before patches) or FORWARD (after patches)
        → NO: continue
     → sub_5B25F0 (scancode→keycode conversion)
     → sub_59C620 (send to remote via protobuf, message type 58)
```

## 5. Alt+Tab Interception (Before Patches)

At `0x5A5324-0x5A535B` in `sub_5A4E20`:

```asm
0x5A532C:  cmp ecx, 2Bh          ; scancode == SDL_SCANCODE_TAB?
0x5A532F:  jnz loc_5A5365        ; no → skip to Alt+Enter check
0x5A5331:  test [esi+20h], dx    ; modifiers & Alt?
0x5A5335:  jz loc_5A5365         ; no Alt → skip
0x5A5337:  cmp dword ptr [esi], 301h  ; event type == KEYUP?
0x5A533D:  jnz loc_5A61D8        ; KEYDOWN → SKIP (filtered!)
0x5A5343:  cmp byte ptr [edi+3AFh], 0
0x5A534A:  jz loc_5A61D8         ; flag not set → skip
0x5A5350:  push [edi+380h]
0x5A5356:  call SDL_MinimizeWindow  ; KEYUP → MINIMIZE!
```

**Result**: Tab KEYDOWN with Alt → silently dropped. Tab KEYUP with Alt → window minimized.

## 6. Binary Patches (4 patches)

Applied in `patcher.cpp`, RVAs from image base `0x400000`:

| # | RVA | Abs Address | Original | Patched | Effect |
|---|-----|-------------|----------|---------|--------|
| 1 | `0x1A533D` | `0x5A533D` | `0F 85 95 0E 00 00` (jnz) | 6× `90` (nop) | Let Tab KEYDOWN through |
| 2 | `0x1A534A` | `0x5A534A` | `0F 84 88 0E 00 00` (jz) | 6× `90` (nop) | Remove flag check |
| 3 | `0x1A5350` | `0x5A5350` | push+call SDL_MinimizeWindow+add esp | 14× `90` (nop) | Remove window minimize |
| 4 | `0x1A5360` | `0x5A5360` | `E9 85 07 00 00` (jmp old) | `E9 E6 01 00 00` (jmp new) | Redirect to forwarding path |

**Result**: Alt+Tab now forwarded to remote via `sub_5B25F0` → `sub_59C620`.

**Note**: Alt+Enter (fullscreen) and Alt+F4 (close) at `0x5A5365` and `0x5A538A` are unaffected.

## 7. SDL3 Keyboard Grab

### What It Is

SDL3 has built-in `SDL_SetWindowKeyboardGrab(window, grabbed)` — designed for VNC/VM/streaming apps. Steam Link's SDL3.dll exports it.

### How It Works

When enabled, SDL3 installs a `WH_KEYBOARD_LL` hook (`WIN_KeyboardHookProc`) that intercepts:

| Key | Scancode | Why |
|-----|----------|-----|
| VK_LWIN / VK_RWIN | LGUI / RGUI | Win key combos |
| VK_LMENU / VK_RMENU | LALT / RALT | Alt combos |
| VK_LCONTROL / VK_RCONTROL | LCTRL / RCTRL | Ctrl combos |
| VK_TAB | TAB | Alt+Tab |
| VK_ESCAPE | ESC | Ctrl+Esc, Alt+Esc |
| VK_SNAPSHOT | PRINTSCREEN | Print Screen |

The hook returns 1 to block these keys from Windows. Other keys pass through.

### The Hint

`SDL_SetHint("SDL_HINT_ALLOW_ALT_TAB_WHILE_GRABBED", "0")` — prevents SDL from minimizing the window when Alt+Tab is pressed while grabbed. **Must be set before enabling grab.**

### Why Patches Are Still Needed

Even with keyboard grab, the binary's filtering at `0x5A533D` still drops Tab when Alt is held. The hook delivers keys via `SDL_SendKeyboardKey()`, which creates SDL events, but the binary then filters them. Patches remove this filtering.

### Flow: Grab + Patches

```
User presses Alt+Tab
  → SDL3 hook intercepts Alt (blocks from Windows)
  → SDL3 hook intercepts Tab (blocks from Windows)
  → SDL_SendKeyboardKey() creates SDL_KEYDOWN events
  → Binary sub_5A4E20 processes Tab+Alt
  → Patches let it through (0x5A533D NOPed)
  → sub_5B25F0 → sub_59C620 → sent to remote host
```

## 8. SDL_WindowData Struct Layout

From `SDL_windowswindow.h` (SDL3), stored as property `"SDL_WindowData"` on the HWND:

```c
struct SDL_WindowData {
    SDL_Window *window;   // +0x00 — pointer to SDL_Window
    HWND hwnd;            // +0x04 — Win32 HWND
    HWND parent;          // +0x08
    HDC hdc;              // +0x0C
    HDC mdc;              // +0x10
    HINSTANCE hinstance;  // +0x14
    // ... more fields
};
```

Retrieved via: `GetPropA(hwnd, "SDL_WindowData")`, then read `SDL_Window*` at offset `+0x00`.

## 9. Scancode / Modifier Reference

### Scancodes

| Key | Scancode | Hex |
|-----|----------|-----|
| Return | 40 | 0x28 |
| Tab | 43 | 0x2B |
| F4 | 61 | 0x3D |
| LCtrl | 224 | 0xE0 |
| LShift | 225 | 0xE1 |
| LAlt | 226 | 0xE2 |
| RGUI | 227 | 0xE3 |
| RCtrl | 228 | 0xE4 |
| RShift | 229 | 0xE5 |
| RAlt | 230 | 0xE6 |
| LGUI | 231 | 0xE7 |

### Modifier Flags

| Flag | Value | Key |
|------|-------|-----|
| SDL_KMOD_LSHIFT | 0x0001 | Left Shift |
| SDL_KMOD_RSHIFT | 0x0002 | Right Shift |
| SDL_KMOD_LCTRL | 0x0040 | Left Ctrl |
| SDL_KMOD_RCTRL | 0x0080 | Right Ctrl |
| SDL_KMOD_LALT | 0x0100 | Left Alt |
| SDL_KMOD_RALT | 0x0200 | Right Alt |
| SDL_KMOD_LGUI | 0x0400 | Left Win |
| SDL_KMOD_RGUI | 0x0800 | Right Win |
| Alt (L+R) | 0x0300 | Used in Alt+Tab/Enter/F4 checks |

## 10. Streaming Protocol

- **Message type**: 58
- **Messages**: `CInputKeyDownMsg` / `CInputKeyUpMsg` (string refs at `0x96F7B5` / `0x96F813`)
- **Fields**: scancode (`0x96F7DB`), modifiers (`0x96F7ED`), keycode (`0x96F800`)
- **Send**: `sub_59C620` → `sub_59BB90(58, buffer)`, protected by critical section at `this + 128298`

## 11. DLL Architecture

### Files

| File | Purpose |
|------|---------|
| `dllmain.cpp` | Main DLL: monitor thread, SDL window lifecycle, grab toggle |
| `patcher.cpp` / `patcher.h` | Binary patching (4 patches) |
| `pch.h` / `framework.h` | Precompiled headers |
| `SteamLinkAltTabCapturerDll.vcxproj` | MSVC v143, Win32, Unicode |

### Lifecycle

```
DLL_PROCESS_ATTACH
  → Create g_hQuit event
  → Spawn MonitorThread

MonitorThread:
  → Wait for SDL3.dll (up to 60s)
  → Load SDL3 functions (SDL_SetWindowKeyboardGrab, SDL_SetHint)
  → Apply binary patches (once)
  → Loop every 500ms:
      → FindSDLWindow() — searches for class "SDL_app"
      → If found & new: AttachSDL()
          → GetSDLWin() — reads SDL_Window* from HWND property
          → GrabOn() — SDL_SetWindowKeyboardGrab(win, 1)
          → Add system menu item ("SDL Keyboard Grab")
          → Subclass window (WndSub)
          → RegisterHotKey (Ctrl+Shift+G)
      → If gone & was attached: DetachSDL()
          → GrabOff()
          → Unsubclass
          → UnregisterHotKey
  → On quit signal: DetachSDL() + return

DLL_PROCESS_DETACH
  → SetEvent(g_hQuit)  // signals monitor thread to exit
```

### Window Search

`FindSDLWindow()` uses `FindWindowExA` recursion (3 levels deep) to avoid `EnumWindows` hangs with Steam Link. Searches for visible windows with class `"SDL_app"` in the current process.

### SDL_Window* Retrieval

`GetSDLWin(HWND)`:
1. `GetPropA(hwnd, "SDL_WindowData")` → gets struct pointer
2. Read `SDL_Window*` at `+0x00`
3. Read `HWND` at `+0x04`, verify it matches
4. If mismatch, try the stored HWND as a child window

### Toggle Controls

- **System menu**: Right-click title bar → "SDL Keyboard Grab"
- **Global hotkey**: Ctrl+Shift+G
- **Check mark**: Shows current grab state

## 12. What's Captured

With grab + patches enabled:

| Shortcut | Captured? | Mechanism |
|----------|-----------|-----------|
| Alt+Tab | ✅ | Binary patch + grab |
| Alt+Esc | ✅ | Grab intercepts both keys |
| Win+Tab | ✅ | Grab intercepts Win + Tab |
| Win+D | ✅ | Grab blocks Win from OS |
| Win+E | ✅ | Grab blocks Win from OS |
| Ctrl+Esc | ✅ | Grab intercepts Ctrl + Esc |
| Ctrl+Shift+Esc | ✅ | Grab intercepts Ctrl + Esc |
| Print Screen | ✅ | Grab intercepts it |
| Alt+F4 | ⚠️ | Alt intercepted, F4 passes (may still close) |
| Ctrl+Alt+Del | ❌ | Kernel-level, cannot intercept |

## 13. Files

```
reverse_engineer/
├─ SteamLink.exe                    # Original binary
├─ SteamLink.exe.i64                # IDA database
├─ findings.md                      # Detailed RE findings
├─ sdl3_keyboard_grab_analysis.md   # SDL3 grab mechanism analysis
└─ (ida working files: .id0/.id1/.id2/.nam/.til)

SteamLinkAltTabCapturerDll/
├─ dllmain.cpp                      # Main DLL source
├─ patcher.cpp                      # Binary patcher
├─ patcher.h                        # Patcher header
├─ pch.h / pch.cpp                  # Precompiled headers
├─ framework.h                      # Windows headers
└─ SteamLinkAltTabCapturerDll.vcxproj

Root:
├─ SteamLinkAltTabCapturer.sln      # Solution file
├─ apply_patches.py                 # PyMem live patching script
├─ enum_w.py                        # Window enumeration debug script
└─ eject.py                         # DLL ejection script
```
