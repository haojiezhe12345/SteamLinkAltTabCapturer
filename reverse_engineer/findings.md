# SteamLink.exe Reverse Engineering Findings

## Binary Overview

- **File**: `F:\Steam Link\SteamLink.exe` (15.7 MB, 32-bit PE)
- **IDA Database**: `SteamLink.exe.i64`
- **Image Base**: `0x400000`
- **Image Size**: `0xF0E000`
- **Dependencies**: SDL3.dll, Qt5*.dll, steamwebrtc.dll
- **Functions**: 17848 total, 792 named
- **Strings**: 86400 total

## Architecture

```
Qt5 UI Layer
  └─ SDL3 Rendering/Input/Streaming Window
       ├─ SDL_PumpEvents / SDL_PeepEvents
       ├─ SDL_AddEventWatch (gamepad filter)
       ├─ SDL_ScreenKeyboardShown
       └─ Streaming Protocol (protobuf)
            ├─ CInputKeyDownMsg (scancode, modifiers, keycode)
            └─ CInputKeyUpMsg   (scancode, modifiers, keycode)
```

## Key Functions

| Function | Address | Purpose |
|---|---|---|
| `sub_5A4E20` | `0x5A4E20` | Main SDL event handler (5374 bytes, massive switch: 88 cases + nested 254-case switch) |
| `sub_5A5270` | `0x5A5270` | SDL_KEYDOWN/SDL_KEYUP case entry (event types 768/769) |
| `sub_5B25F0` | `0x5B25F0` | Key event processing (scancode→keycode via SDL_GetKeyFromScancode, sends to streaming) |
| `sub_59C620` | `0x59C620` | Sends CInputKeyDownMsg/CInputKeyUpMsg to remote host (message type 58) |
| `sub_5B4DC0` | `0x5B4DC0` | Alt+Enter handler (fullscreen toggle) |
| `sub_5B2C30` | `0x5B2C30` | Alt+F4 handler (close/quit) |
| `sub_5BBF60` | `0x5BBF60` | "Hold %s key to control remote session" toast |
| `sub_5AA4C0` | `0x5AA4C0` | Get current overlay grab key |
| `sub_5AA0C0` | `0x5AA0C0` | Get active overlay |
| `sub_567C60` | `0x567C60` | SDL event filter for gamepad only (not keyboard) |
| `sub_5ABD30` | `0x5ABD30` | Main init (calls SDL_AddEventWatch, SDL_InitSubSystem) |
| `sub_5AC820` | `0x5AC820` | Main loop (calls SDL_PumpEvents + sub_5A72E0) |

## Keyboard Event Flow

```
SDL_KEYDOWN/SDL_KEYUP (event types 768/769)
  └─ sub_5A4E20 (main SDL event handler)
       ├─ Special key checks (Tab, Enter, F4 with Alt modifier)
       ├─ Overlay handling (sub_5AA0C0 → vtable calls)
       ├─ On-screen keyboard filtering (SDL_ScreenKeyboardShown)
       └─ sub_5B25F0 (key event processing)
            ├─ SDL_GetKeyFromScancode (scancode → keycode)
            └─ sub_59C620 (send to remote via streaming protocol)
                 ├─ sub_59EF40 (get message buffer)
                 ├─ Message type = 58 (CInputKeyDownMsg/CInputKeyUpMsg)
                 └─ sub_59BB90 (send message)
```

## Alt+Tab Interception (Before Patches)

The binary intercepted Alt+Tab at `sub_5A4E20` in the keyboard event handler:

```asm
0x5A5324:  mov ecx, [esi+18h]       ; ecx = scancode
0x5A5327:  mov edx, 300h            ; edx = KMOD_LALT | KMOD_RALT
0x5A532C:  cmp ecx, 2Bh             ; if scancode == SDL_SCANCODE_TAB (43)
0x5A532F:  jnz loc_5A5365           ; if not Tab, skip to Alt+Enter check
0x5A5331:  test [esi+20h], dx       ; if (modifiers & Alt) == 0
0x5A5335:  jz loc_5A5365            ; if no Alt, skip
0x5A5337:  cmp dword ptr [esi], 301h ; if event type != SDL_KEYUP
0x5A533D:  jnz loc_5A61D8           ; KEY DOWN: SKIP (not forwarded to remote)
0x5A5343:  cmp byte ptr [edi+3AFh], 0
0x5A534A:  jz loc_5A61D8            ; if flag not set, skip
0x5A5350:  push dword ptr [edi+380h]
0x5A5356:  call SDL_MinimizeWindow   ; KEY UP: MINIMIZE WINDOW
0x5A535B:  add esp, 4
```

**Behavior**:
- **Tab KEYDOWN** with Alt: Event **skipped** (never forwarded to remote)
- **Tab KEYUP** with Alt: Window **minimized** via `SDL_MinimizeWindow`

## Alt+Enter and Alt+F4 (Unaffected by Patches)

| Combination | Scancode | Modifier | Key Down | Key Up |
|---|---|---|---|---|
| Alt+Tab | 0x2B (43) | 0x300 (Alt) | Skipped | SDL_MinimizeWindow |
| Alt+Enter | 0x28 (40) | 0x300 (Alt) | Skipped | sub_5B4DC0 (fullscreen toggle) |
| Alt+F4 | 0x3D (61) | 0x300 (Alt) | Skipped | sub_5B2C30 (close/quit) |

## SDL3 Event Types (Observed in Switch)

| Case | Event Type | Purpose |
|---|---|---|
| 768 | SDL_KEYDOWN | Keyboard key pressed |
| 769 | SDL_KEYUP | Keyboard key released |
| 771 | SDL_TEXTINPUT | Text input event |
| 1616-1629 | Gamepad events | Handled by sub_567C60 event filter |

## Scancode Reference

| Scancode | Hex | Key |
|---|---|---|
| 40 | 0x28 | SDL_SCANCODE_RETURN |
| 43 | 0x2B | SDL_SCANCODE_TAB |
| 61 | 0x3D | SDL_SCANCODE_F4 |
| 224 | 0xE0 | SDL_SCANCODE_LCTRL |
| 225 | 0xE1 | SDL_SCANCODE_LSHIFT |
| 226 | 0xE2 | SDL_SCANCODE_LALT |
| 227 | 0xE3 | SDL_SCANCODE_RGUI |
| 228 | 0xE4 | SDL_SCANCODE_RCTRL |
| 229 | 0xE5 | SDL_SCANCODE_RSHIFT |
| 230 | 0xE6 | SDL_SCANCODE_RALT |
| 231 | 0xE7 | SDL_SCANCODE_LGUI |

## Modifier Flags

| Flag | Value | Key |
|---|---|---|
| SDL_KMOD_LSHIFT | 0x0001 | Left Shift |
| SDL_KMOD_RSHIFT | 0x0002 | Right Shift |
| SDL_KMOD_LCTRL | 0x0040 | Left Ctrl |
| SDL_KMOD_RCTRL | 0x0080 | Right Ctrl |
| SDL_KMOD_LALT | 0x0100 | Left Alt |
| SDL_KMOD_RALT | 0x0200 | Right Alt |
| SDL_KMOD_LGUI | 0x0400 | Left Win |
| SDL_KMOD_RGUI | 0x0800 | Right Win |
| Alt (L+R) | 0x0300 | Used in Alt+Tab/Enter/F4 checks |

## Protobuf Streaming Protocol

### Message Types
- `CInputKeyDownMsg` — string at `0x96F7B5`
- `CInputKeyUpMsg` — string at `0x96F813`
- `k_EStreamControlInputKeyDown` — string at `0x9712FB`
- `k_EStreamControlInputKeyUp` — string at `0x97131D`

### CInputKeyDownMsg Fields (at `0x96F7B5`)
| Field | Address | Purpose |
|---|---|---|
| scancode | `0x96F7DB` | SDL scancode |
| modifiers | `0x96F7ED` | Modifier flags |
| keycode | `0x96F800` | SDL keycode |

### Message Sending
- Message type = 58 (used in `sub_59C620`)
- Sent via `sub_59BB90(58, buffer)`
- Protected by critical section at `this + 128298`

## Patches Applied (Live Process)

### Applied to running SteamLink.exe (PID 203088)

| # | Address | Original | Patched | Purpose |
|---|---|---|---|---|
| 1 | `0xf3533d` | `0F 85 95 0E 00 00` | `90 90 90 90 90 90` | Remove Alt+Tab KEYDOWN skip (jnz → nop) |
| 2 | `0xf3534a` | `0F 84 88 0E 00 00` | `90 90 90 90 90 90` | Remove flag check skip (jz → nop) |
| 3 | `0xf35350` | `FF B7 80 03 00 00 E8 DF 22 10 00 83 C4 04` | 14x `90` | Remove SDL_MinimizeWindow call |
| 4 | `0xf35360` | `E9 85 07 00 00` | `E9 E6 01 00 00` | Redirect jmp to normal key forwarding path |

### Effect
- **Before**: Alt+Tab → Tab KEYDOWN skipped, Tab KEYUP minimizes window
- **After**: Alt+Tab → both KEYDOWN and KEYUP forwarded to remote host via `sub_5B25F0` → `sub_59C620`

### Note
Alt+Enter (fullscreen toggle) and Alt+F4 (close) handlers remain **unaffected** — their code at `0x5A5365` and `0x5A538A` is not touched.

## SteamLinkAltTabCapturerDll Integration

The patches are designed to work **with** the DLL:

1. DLL captures Alt+Tab via `WH_KEYBOARD_LL` hook (intercepts before Windows)
2. DLL sends WM_KEYDOWN for Tab (with Alt modifier) to SDL3 window
3. SDL3 creates SDL_KEYDOWN event with scancode 0x2B and modifier 0x300
4. **Without patches**: Binary filters Tab at `0x5A533D` (skips it)
5. **With patches**: Binary forwards Tab to remote via `sub_5B25F0`

## Other Notable Code

### Key Remapping (at 0x5A5270)
```asm
0x5A5270:  mov byte ptr [edi+0F5h], 1    ; Set keyboard event flag
0x5A5276:  mov eax, [esi+18h]            ; Load scancode
0x5A5279:  cmp eax, 11Ah                  ; If scancode == 0x11A (282)
0x5A527E:  jnz short loc_5A528D
0x5A5280:  mov dword ptr [esi+18h], 29h  ; Remap to Escape (41)
```

### On-Screen Keyboard Filtering (at 0x5A5526)
```asm
0x5A5526:  push [edi+380h]
0x5A552C:  call SDL_ScreenKeyboardShown
0x5A5534:  test al, al
0x5A5536:  jz short loc_5A554B           ; If not shown, skip
0x5A5538:  mov eax, [esi+1Ch]            ; Load keycode
0x5A553B:  cmp eax, 20h                  ; If keycode < 0x20 (space)
0x5A553E:  jb short loc_5A554B           ; Skip
0x5A5540:  cmp eax, 40000000h            ; If keycode < SDLK_SCANCODE_MASK
0x5A5545:  jb def_5A4E86                 ; Don't forward (printable char filtered)
```

### Normal Key Forwarding (at 0x5A554B)
```asm
0x5A554B:  cmp dword ptr [esi], 300h     ; If event type == SDL_KEYDOWN
0x5A5551:  mov ecx, edi
0x5A5553:  setz al                        ; al = keyDown ? 1 : 0
0x5A5556:  movzx eax, al
0x5A5559:  push eax                       ; arg3: keyDown
0x5A555A:  movzx eax, word ptr [esi+20h] ; Load modifiers
0x5A555E:  push eax                       ; arg2: modifiers
0x5A555F:  push dword ptr [esi+18h]       ; arg1: scancode
0x5A5562:  call sub_5B25F0                ; Forward to streaming protocol
```

## Streaming Files

- `streamplayer.bin` — loaded from `F:\Steam Link`
- `streaminput.bin` — loaded from `F:\Steam Link`
- Streaming client source references: `streamclient.cpp` (`0x92DAE0`), `streamplayer.cpp` (`0x94A394`), `streamplayerhiddevices.cpp` (`0x94B394`)
