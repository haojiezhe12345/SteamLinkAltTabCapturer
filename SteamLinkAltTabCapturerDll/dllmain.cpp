#include "pch.h"
#include "log.h"
#include "patcher.h"
#include "sdl.h"
#include "menu.h"

// ============================================================================
// Module-scope state
// ============================================================================

static HWND          g_sdlHwnd    = NULL;
static SDL_Window*   g_pWin       = NULL;
static BOOL          g_grabbed    = FALSE;
static volatile LONG g_quit       = 0;
static volatile LONG g_threadExit = 0;
static HANDLE        g_hThread    = NULL;

// ============================================================================
// Attach / Detach
// ============================================================================

static void AttachSDL(HWND sdlHwnd)
{
    if (sdlHwnd == g_sdlHwnd)
        return;

    g_sdlHwnd = sdlHwnd;
    Log("SDL_app window: %p", g_sdlHwnd);

    g_pWin = SDL3_GetWindow(g_sdlHwnd);
    if (g_pWin)
    {
        Log("SDL_Window*: %p", g_pWin);
        SDL3_SetHint("SDL_HINT_ALLOW_ALT_TAB_WHILE_GRABBED", "0");
        SDL3_SetKeyboardGrab(g_pWin, TRUE);
        g_grabbed = TRUE;
    }
    else
    {
        Log("SDL_Window* not found");
    }

    Menu_InitSubclass(g_sdlHwnd, g_pWin, &g_grabbed);
}

static void DetachSDL(void)
{
    if (!g_sdlHwnd)
        return;

    Menu_CleanupSubclass(g_sdlHwnd);
    SDL3_SetKeyboardGrab(g_pWin, FALSE);
    g_grabbed = FALSE;
    g_sdlHwnd = NULL;
    g_pWin    = NULL;
    Log("Detached");
}

// ============================================================================
// Monitor thread
// ============================================================================

static DWORD WINAPI MonitorThread(LPVOID lpParam)
{
    (void)lpParam;

    for (int i = 0; i < 600; i++)
    {
        if (g_quit) return 0;
        if (GetModuleHandleA("SDL3.dll"))
            break;
        Sleep(100);
    }

    if (!SDL3_Init())
    {
        Log("SDL3_Init failed");
        return 1;
    }

    PatchBin();
    Log("Watching for SDL_app windows... (tid=%lu)", GetCurrentThreadId());

    BOOL wasAttached = FALSE;

    for (;;)
    {
        if (g_quit) break;

        HWND sdlHwnd = FindSDLWindow();

        if (sdlHwnd)
        {
            if (sdlHwnd != g_sdlHwnd)
                AttachSDL(sdlHwnd);
            wasAttached = TRUE;
        }
        else if (wasAttached)
        {
            Log("SDL_app gone, detaching");
            DetachSDL();
            wasAttached = FALSE;
        }

        Sleep(500);
    }

    DetachSDL();

    Log("Monitor thread exiting");
    InterlockedExchange(&g_threadExit, 1);

    return 0;
}

// ============================================================================
// DllMain
// ============================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ulReasonForCall, LPVOID lpReserved)
{
    (void)lpReserved;

    switch (ulReasonForCall)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        Log("Attached to tid: %lu", GetCurrentThreadId());
        g_hThread = CreateThread(NULL, 0, MonitorThread, NULL, 0, NULL);
        break;

    case DLL_PROCESS_DETACH:
        InterlockedExchange(&g_quit, 1);
        if (g_hThread)
        {
            // CRUCIAL - DO NOT REMOVE THIS SPIN LOCK
			// Unloading the DLL before its thread exits will cause Access Violation.
            for (;;) {
                // Why use spinlock:
                // When unloading the DLL manually, MonitorThread will NOT exit until the DLL has been fully unloaded, even after the thread function returns a value.
                // So we can't rely on WaitForSingleObject here. It will hang forever.
                if (g_threadExit)
                {
                    Sleep(100); // Ensure the thread has actually stopped executing code
                    break;
                }
                // We still have to check whether the thread is alive because:
				// When the process is exiting, our MonitorThread will be terminated by the system before g_threadExit is set to true.
                // In such circumstances, checking for g_threadExit will hang forever.
				// If we hang here, the process will not be able to exit because our DLL stucks unloading.
                DWORD waitResult = WaitForSingleObject(g_hThread, 0);
				if (waitResult == WAIT_OBJECT_0) break;
                Sleep(100);
            }
            CloseHandle(g_hThread);
            g_hThread = NULL;
        }
        break;
    }

    return TRUE;
}
