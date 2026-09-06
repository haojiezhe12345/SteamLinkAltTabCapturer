# SDL3 Native System Shortcut Capture Analysis

## SDL3 Has Built-in Keyboard Grab

SDL3 provides `SDL_SetWindowKeyboardGrab(window, grabbed)` — designed **exactly** for VNC clients, VM frontends, and game streaming applications like Steam Link.

**Steam Link's SDL3.dll exports this function.**

## How It Works (Windows)

When keyboard grab is enabled, SDL3 installs a **low-level keyboard hook** (`WIN_KeyboardHookProc`) via `SetWindowsHookEx(WH_KEYBOARD_LL, ...)`.

The hook intercepts these keys and **blocks them from Windows** (returns 1):

| VK Code | Scancode | Key |
|---|---|---|
| VK_LWIN | SDL_SCANCODE_LGUI | Left Windows |
| VK_RWIN | SDL_SCANCODE_RGUI | Right Windows |
| VK_LMENU | SDL_SCANCODE_LALT | Left Alt |
| VK_RMENU | SDL_SCANCODE_RALT | Right Alt |
| VK_LCONTROL | SDL_SCANCODE_LCTRL | Left Ctrl |
| VK_RCONTROL | SDL_SCANCODE_RCTRL | Right Ctrl |
| VK_SNAPSHOT | SDL_SCANCODE_PRINTSCREEN | Print Screen |
| VK_TAB | SDL_SCANCODE_TAB | Tab (for Alt+Tab) |
| VK_ESCAPE | SDL_SCANCODE_ESCAPE | Escape (for Alt+Esc) |

All other keys pass through to the next hook normally.

## What Gets Captured

| Shortcut | Captured? | Notes |
|---|---|---|
| Alt+Tab | ✅ Yes | Both Alt and Tab intercepted |
| Alt+Esc | ✅ Yes | Both Alt and Esc intercepted |
| Win key alone | ✅ Yes | Start menu blocked |
| Win+Tab | ✅ Yes | Win intercepted, Tab intercepted |
| Win+D | ✅ Yes | Win intercepted, D passes through but OS sees no Win modifier |
| Win+E | ✅ Yes | Win intercepted, E passes through but OS sees no Win modifier |
| Ctrl+Esc | ✅ Yes | Both Ctrl and Esc intercepted |
| Ctrl+Shift+Esc | ✅ Yes | Ctrl and Esc intercepted |
| Alt+F4 | ⚠️ Partial | Alt intercepted, F4 passes through (may still work) |
| Ctrl+Alt+Del | ❌ No | Windows prevents this at kernel level |

## The Problem: Binary Still Filters

Even with keyboard grab, the binary's code at `0x5A533D` still filters Tab when Alt is held. The keyboard hook delivers the key via `SDL_SendKeyboardKey()`, which puts an SDL event in the queue. The binary then processes this event and filters it.

**Flow with keyboard grab only:**
```
User presses Alt+Tab
  → SDL3 hook intercepts Alt (blocks from Windows)
  → SDL3 hook intercepts Tab (blocks from Windows)
  → SDL_SendKeyboardKey() creates SDL_KEYDOWN events
  → Binary sub_5A4E20 processes Tab+Alt
  → FILTERED at 0x5A533D (Tab skipped)
  → Tab never reaches remote host
```

**Flow with keyboard grab + patches:**
```
User presses Alt+Tab
  → SDL3 hook intercepts Alt (blocks from Windows)
  → SDL3 hook intercepts Tab (blocks from Windows)
  → SDL_SendKeyboardKey() creates SDL_KEYDOWN events
  → Binary sub_5A4E20 processes Tab+Alt
  → Patches let it through (0x5A533D NOPed)
  → sub_5B25F0 → sub_59C620 → sent to remote host
```

## Recommended Approach: Hook + Patches

### Option A: DLL Injection (Best)

Inject a DLL that:
1. Sets hint: `SDL_SetHint("SDL_HINT_ALLOW_ALT_TAB_WHILE_GRABBED", "0")`
2. Finds the SDL window handle
3. Calls `SDL_SetWindowKeyboardGrab(window, true)`
4. The keyboard hook intercepts all system shortcuts
5. Binary patches forward them to remote host

### Option B: Patches Only (Current)

The 4 patches we applied handle Alt+Tab specifically. But:
- Does NOT capture Win+D, Win+E, Win+Tab, Ctrl+Esc, etc.
- Only handles the Alt+Tab case

### Option C: Both (Recommended)

Use keyboard grab for comprehensive capture + patches to fix the binary's filtering.

## SDL_HINT_ALLOW_ALT_TAB_WHILE_GRABBED

By default, SDL emulates Alt+Tab while grabbed (minimizes the window). This hint controls that behavior:

| Value | Behavior |
|---|---|
| `"0"` | SDL does NOT handle Alt+Tab. Application is responsible. |
| `"1"` | SDL minimizes window on Alt+Tab (default) |

Setting this to `"0"` is essential — otherwise SDL will minimize the window even with keyboard grab enabled.

## Source Code Reference

From `src/video/windows/SDL_windowsevents.c`:

```c
LRESULT CALLBACK
WIN_KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    KBDLLHOOKSTRUCT *hookData = (KBDLLHOOKSTRUCT *)lParam;
    SDL_VideoData *data = SDL_GetVideoDevice()->internal;
    SDL_Scancode scanCode;

    if (nCode < 0 || nCode != HC_ACTION) {
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }
    if (hookData->scanCode == 0x21d) {
        return 1;  // Skip fake LCtrl when RAlt is pressed
    }

    switch (hookData->vkCode) {
    case VK_LWIN:    scanCode = SDL_SCANCODE_LGUI; break;
    case VK_RWIN:    scanCode = SDL_SCANCODE_RGUI; break;
    case VK_LMENU:   scanCode = SDL_SCANCODE_LALT; break;
    case VK_RMENU:   scanCode = SDL_SCANCODE_RALT; break;
    case VK_LCONTROL: scanCode = SDL_SCANCODE_LCTRL; break;
    case VK_RCONTROL: scanCode = SDL_SCANCODE_RCTRL; break;
    case VK_SNAPSHOT: scanCode = SDL_SCANCODE_PRINTSCREEN; break;
    case VK_TAB:     scanCode = SDL_SCANCODE_TAB; break;
    case VK_ESCAPE:  scanCode = SDL_SCANCODE_ESCAPE; break;
    default:
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }

    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
        if (!data->raw_keyboard_enabled) {
            SDL_SendKeyboardKey(0, SDL_GLOBAL_KEYBOARD_ID, hookData->scanCode, scanCode, true);
        }
    } else {
        if (!data->raw_keyboard_enabled) {
            SDL_SendKeyboardKey(0, SDL_GLOBAL_KEYBOARD_ID, hookData->scanCode, scanCode, false);
        }
    }
    return 1;  // Block the key from Windows
}
```

## Key Insight

The hook only intercepts **modifier keys** + **Tab/Esc/PrintScreen**. It does NOT intercept regular keys like D, E, F4, etc. However, by blocking the modifier keys from Windows, the OS never sees the modifier state, so system shortcuts that depend on modifiers (Win+D, Win+E, etc.) are effectively blocked.
