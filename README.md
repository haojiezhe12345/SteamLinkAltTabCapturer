# Steam Link Shortcut Key Capturer

Let Steam Link forward system keyboard shortcuts (Alt+Tab, Win+D, Ctrl+Esc, etc.) to the remote host. For remote desktop use.

## Usage

Download Steam Link 1.3.33 from https://media.steampowered.com/steamlink/windows/SteamLink-1.3.33.zip

Download and extract `SteamLinkAltTabCapturer.zip` from releases.

### Install

Run `Setup.bat` **as Administrator**.

<img width="757" height="519" alt="image" src="https://github.com/user-attachments/assets/e4cbea03-7ac7-4d48-9ef3-e7787c02c773" />

This registers the wrapper as a debugger for `SteamLink.exe`:

```
HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\SteamLink.exe
    Debugger = "D:\Path\To\Extracted\Zip\SteamLinkAltTabWrapper.exe"
```

Every time Steam Link starts, the wrapper injects the DLL automatically.

### Uninstall

Run `Uninstall.bat` as Administrator. This removes the above registry key.

### Advanced usage

#### Manual launch

You can launch the wrapper directly without registering it as a debugger:
```
SteamLinkAltTabWrapper.exe "C:\Program Files (x86)\Steam Link\SteamLink.exe"
```

#### Manual injection

If you have a DLL injector (e.g. Process Hacker, System Informer), inject `SteamLinkAltTabCapturerDll.dll` into `SteamLink.exe` after it starts. The DLL's monitor thread will detect the SDL (streaming) window automatically.

### Toggle keyboard capture on / off

After the DLL is injected, start a streaming session, keyboard grab will be enabled automatically.  
You can toggle it on/off by **right-clicking the window title**:

<img width="505" height="228" alt="image" src="https://github.com/user-attachments/assets/2397aa97-405d-46ae-b04e-90a83e899d5f" />


| Action | Trigger |
|--------|---------|
| Toggle keyboard grab | Right-click SDL window title bar → **SDL Keyboard Grab** |
| Toggle keyboard grab | `Ctrl+Shift+G` (global hotkey) |

When grab is enabled, a checkmark appears in the system menu.

#### What's captured:

| Shortcut | Forwarded? | Notes |
|----------|------------|-------|
| Alt+Tab | Yes | Binary patch + grab |
| Alt+Esc | Yes | Grab |
| Win+Tab | Yes | Grab |
| Win+D | Yes | Grab |
| Win+E | Yes | Grab |
| Ctrl+Esc | Yes | Grab |
| Ctrl+Shift+Esc | Yes | Grab |
| Print Screen | Yes | Grab |
| Alt+F4 | Partial | Alt blocked, F4 passes through |
| Ctrl+Alt+Del | No | Kernel-level, cannot be intercepted |

## Development

### How it works

Two mechanisms work together:

1. **Binary patches** — NOP 4 code sites in `SteamLink.exe` that filter Alt+Tab and minimize the window, so Tab key events flow through to the remote host.
2. **SDL3 keyboard grab** — call `SDL_SetWindowKeyboardGrab` at runtime, which installs a `WH_KEYBOARD_LL` OS-level hook that intercepts modifier keys (Alt, Ctrl, Win) and system keys (Tab, Esc, PrtScn) before Windows handles them.

Patches alone don't capture Win+key or Ctrl+Esc. Grab alone doesn't fix the binary's filtering. Both are needed.

#### Lifecycle

```
SteamLinkAltTabWrapper.exe
  └─ CreateProcess(DEBUG_ONLY_THIS_PROCESS)
       └─ DebugActiveProcessStop (detach)
            └─ InjectDll (VirtualAllocEx + CreateRemoteThread → LoadLibraryA)
                 └─ SteamLinkAltTabCapturerDll.dll
                      ├─ DllMain → spawns MonitorThread
                      ├─ MonitorThread: waits for SDL3.dll, applies patches, polls for SDL_app window
                      │    ├─ AttachSDL: gets SDL_Window*, enables keyboard grab, subclasses window
                      │    └─ DetachSDL: disables grab, removes subclass, waits for next streaming session
                      ├─ patcher.cpp: 4 binary patches (Alt+Tab filtering removal)
                      ├─ sdl.cpp: SDL3 function pointers, FindSDLWindow, GetWindow, keyboard grab
                      └─ menu.cpp: system menu "SDL Keyboard Grab" toggle + Ctrl+Shift+G global hotkey
```

1. Wrapper injects DLL early in SteamLink's startup, before any UI appears.
2. Monitor thread waits up to 60s for `SDL3.dll` to load (SteamLink loads it on demand).
3. Once SDL3 is available, patches are applied once and function pointers are resolved.
4. Monitor thread polls every 500ms for an `SDL_app` class window. The SDL window only exists while streaming.
5. When a streaming session starts: grab is enabled, system menu is patched, hotkey is registered.
6. When streaming stops: grab is disabled, subclass removed, thread waits for the next session.
7. When the main thread exits: monitor thread detects it and terminates.

### Building

Requires [Visual Studio 2022](https://visualstudio.microsoft.com/) or later with **Desktop development with C++** workload (MSVC v143 toolset).

```
MSBuild.exe SteamLinkAltTabCapturer.sln /p:Configuration=Debug /p:Platform=x86
```

Or open `SteamLinkAltTabCapturer.sln` in Visual Studio and build (`Ctrl+Shift+B`). All projects must be built as **x86** (32-bit), matching the Steam Link binary.

#### Projects

| Project | Output | Purpose |
|---------|--------|---------|
| `SteamLinkAltTabCapturerDll` | `SteamLinkAltTabCapturerDll.dll` | Injected DLL — applies patches, enables keyboard grab, adds system menu toggle |
| `SteamLinkAltTabWrapper` | `SteamLinkAltTabWrapper.exe` | Launcher — starts SteamLink.exe with `DEBUG_ONLY_THIS_PROCESS` and injects the DLL |
| `SteamLinkInputTester` | `SteamLinkInputTester.exe` | Test tool for keyboard input forwarding |

#### Build output

```
Debug\  (or Release\)
├── SteamLinkAltTabWrapper.exe
├── SteamLinkAltTabCapturerDll.dll
├── SteamLinkInputTester.exe
├── Setup.bat                          # IFEO registration (run as admin)
└── Uninstall.bat                      # IFEO removal (run as admin)
```

### Binary patches

Four patches in `SteamLink.exe` (RVA from image base `0x400000`):

| RVA | Size | Description |
|-----|------|-------------|
| `0x1A533D` | 6 bytes NOP | Remove `jnz` that skips Tab KEYDOWN when Alt is held |
| `0x1A534A` | 6 bytes NOP | Remove `jz` flag check that suppresses forwarding |
| `0x1A5350` | 14 bytes NOP | Remove `push + call SDL_MinimizeWindow + add esp` |
| `0x1A5360` | 5 bytes JMP | Redirect `jmp` to normal key forwarding path |

Patches are applied once per process lifetime and persist across streaming sessions.

### File overview

```
SteamLinkAltTabCapturer/
├── SteamLinkAltTabCapturer.sln
├── README.md
│
├── SteamLinkAltTabCapturerDll/        # Injected DLL
│   ├── dllmain.cpp                    # DllMain, MonitorThread, AttachSDL, DetachSDL
│   ├── sdl.cpp / sdl.h                # FindSDLWindow, SDL3_GetWindow, SDL3_SetKeyboardGrab, SDL3_SetHint
│   ├── menu.cpp / menu.h              # System menu toggle, subclass procedure, global hotkey
│   ├── patcher.cpp / patcher.h        # Binary patches (Alt+Tab filtering removal)
│   ├── log.cpp / log.h                # Debug output logging ([SLCap] prefix)
│   ├── pch.h / pch.cpp                # Precompiled header
│   └── framework.h                    # Windows headers (includes tlhelp32.h)
│
├── SteamLinkAltTabWrapper/            # Launcher / injector
│   └── SteamLinkAltTabWrapper.cpp     # CreateProcess + inject DLL
│
├── SteamLinkInputTester/              # Test tool
│
└── reverse_engineer/                  # Research artifacts
    ├── summary.md                     # Full reverse engineering findings
    ├── findings.md                    # Detailed binary analysis
    ├── sdl3_keyboard_grab_analysis.md # SDL3 grab mechanism analysis
    └── SteamLink.exe.i64              # IDA Pro database
```

### Troubleshooting

**Debug output:** Use [DebugView](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview) or attach a debugger to SteamLink.exe. Look for `[SLCap]` prefixed messages.

| Message | Meaning |
|---------|---------|
| `[SLCap] Attached (main tid=XXX)` | DLL loaded |
| `[SLCap] Watching for SDL_app windows... (tid=YYY)` | Monitor running |
| `[SLCap] Grab ON` / `[SLCap] Grab OFF` | Grab state changed |
| `[SLCap] Mismatch: ...` | Binary version mismatch (patches don't match) |
| `[SLCap] SDL3_Init failed` | SDL3.dll not found or missing exports |

**Patches don't apply:** The binary version may differ from what the patcher expects. If Steam Link has been updated, the patches need to be re-derived for the new version.

**Grab doesn't work:** Ensure streaming is active (the `SDL_app` window must exist). The DLL only enables grab when it detects a streaming session.

### Notes

- SteamLink.exe is 32-bit — all projects must be built as x86
- The SDL window (`SDL_app` class) only exists while streaming; the DLL watches for its creation and destruction
- The wrapper uses `DEBUG_ONLY_THIS_PROCESS` to prevent IFEO recursion (the wrapper is registered as SteamLink's debugger)
- Alt+F4 cannot be fully captured because the Alt modifier is intercepted by the grab, preventing the remote host from seeing the Alt+F4 combination
- The DLL uses `OutputDebugStringA` for logging — no files, no console window
